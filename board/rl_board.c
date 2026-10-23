/*
 * Board baselines: the serial and double-buffered handoff between NVDLA and
 * SAT-Accel on the ESP SoC (Ariane, Linux), every phase timed with rdcycle.
 * The board counterpart of baselines/harness.c (P1), with the real
 * accelerators in place of the trace replay and the MiniSat stand-in.
 *
 * Per puzzle:
 *   nn      NVDLA classifies every given cell (rl_nvdla; --nn trace replays
 *           the int8 emulator's logits instead)               T_nn
 *   argmax  logits -> unit literals (rl_argmax_units, as P1)  T_conv
 *   image   base CNF + units -> SAT-Accel memory image        T_conv
 *   xfer    image -> the tile's DMA buffers (sa_dev_load)     T_xfer
 *   solve   start the tile, wait for its interrupt            T_sync + T_sat
 *           (T_sat = the tile's busy counter; T_sync = the rest of the wait)
 *
 * serial:  one buffer set; each phase waits for the previous one.
 * double:  two buffer sets in the tile's DMA memory: while the tile solves
 *          puzzle i from one set, the CPU runs NVDLA, the conversion and the
 *          transfer for puzzle i+1 into the other (proposal's double-buffered
 *          baseline). stall = time spent waiting for the tile.
 *
 * Every answer is checked: SAT/UNSAT and grid against the PySAT reference
 * (identical answers across runs are checked by the summary script), and
 * NVDLA's logits against the emulator's (nn_mismatch).
 *
 * --handoff delta (ReasonLink mechanism iii as a CPU-only optimization, the
 * proposal's ablation): each buffer set holds the base formula's image,
 * loaded once outside the timed run; a puzzle's givens are written only as
 * level-0 fixed decisions (sa_dev_load_decisions), with no image build.
 *
 * Usage: rl_board --mode serial|double [--clear full|lean] [--xfer allocated|used]
 *          [--handoff full|delta] [--nn nvdla|trace] [--n N] [--data DIR]
 *          [--loadable FILE] [--csv FILE]
 */
#define _GNU_SOURCE
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "cnf.h"
#include "rl_nvdla.h"
#include "rl_timer.h"
#include "sataccel_caps.h"
#include "sataccel_ioctl.h"
#include "sataccel_linux.h"
#include "trace.h"

#define IMG 784

typedef struct {
    uint32_t num_puzzles, num_images;
    uint8_t *images;        /* num_images x 784 */
    uint8_t *rec;           /* per puzzle: i16 idx[81], i8 ip2[81][10] */
} cells_t;

#define REC_BYTES (RL_CELLS * 2 + RL_CELLS * 10)

static int cells_read(cells_t *c, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t h[24];
    uint32_t v[5];
    if (fread(h, 1, 24, f) != 24 || memcmp(h, "RLBC", 4)) { fclose(f); return -1; }
    memcpy(v, h + 4, sizeof v);
    if (v[0] != 1 || v[3] != IMG || v[4] != 10) { fclose(f); return -1; }
    c->num_puzzles = v[1];
    c->num_images = v[2];
    c->images = malloc((size_t)c->num_images * IMG);
    c->rec = malloc((size_t)c->num_puzzles * REC_BYTES);
    int ok = c->images && c->rec &&
             fread(c->images, IMG, c->num_images, f) == c->num_images &&
             fread(c->rec, REC_BYTES, c->num_puzzles, f) == c->num_puzzles;
    fclose(f);
    return ok ? 0 : -1;
}

static int16_t cell_image(const cells_t *c, uint32_t p, int cell)
{
    int16_t v;
    memcpy(&v, c->rec + (size_t)p * REC_BYTES + 2 * cell, 2);
    return v;
}

static const int8_t *cell_ip2(const cells_t *c, uint32_t p, int cell)
{
    return (const int8_t *)(c->rec + (size_t)p * REC_BYTES + 2 * RL_CELLS + 10 * cell);
}

typedef struct {
    uint64_t nn, argmax, image, xfer, sync, stall, sat;
    uint64_t tile_busy, tile_active, tile_stall;
    size_t xfer_bytes;
    uint32_t givens, nn_mismatch;
    int sat_flag;
    uint32_t decisions, conflicts;
    int8_t grid[RL_CELLS];
} rec_t;

/* One SAT-Accel buffer set and the puzzle currently in it */
typedef struct {
    sa_dev dev;
    sa_image img;
    rl_cnf inst;
    uint32_t puzzle;
    uint64_t t_start;
} slot_t;

static sa_linux S;
static rl_nvdla *NV;
static cells_t CELLS;
static rl_cnf BASE;
static rl_reference REF;
static int USE_TRACE, LEAN_XFER, DELTA;
static rec_t *R;

/* nn + conv + xfer for puzzle p into slot s */
static void prepare(slot_t *s, uint32_t p)
{
    rec_t *r = &R[p];
    uint8_t buf[RL_CELLS + RL_CELLS * 16];
    rl_trace tr = {.num_puzzles = 1, .cells = RL_CELLS, .classes = 10, .stride = 16, .ip2_scale = 0, .data = buf};
    memset(buf, 0, sizeof buf);

    uint64_t t0 = rl_cycles();
    for (int cell = 0; cell < RL_CELLS; cell++) {
        int16_t im = cell_image(&CELLS, p, cell);
        if (im < 0) continue;
        int8_t *out = (int8_t *)buf + RL_CELLS + 16 * cell;
        buf[cell] = 1;
        r->givens++;
        if (USE_TRACE) {
            memcpy(out, cell_ip2(&CELLS, p, cell), 10);
        } else {
            if (rl_nvdla_classify(NV, CELLS.images + (size_t)im * IMG, out)) {
                fprintf(stderr, "rl_board: NVDLA submit failed (puzzle %u)\n", p);
                exit(1);
            }
            if (memcmp(out, cell_ip2(&CELLS, p, cell), 10)) r->nn_mismatch++;
        }
    }
    uint64_t t1 = rl_cycles();
    int32_t units[RL_CELLS];
    uint32_t k = rl_argmax_units(&tr, 0, units);
    uint64_t t2 = rl_cycles();
    size_t bytes;
    uint64_t t3;
    if (DELTA) {
        /* s->img is the base formula's image, already in this slot's buffers:
         * the givens go in as fixed decisions (no image build) */
        t3 = rl_cycles();
        bytes = sa_dev_load_decisions(&s->dev, &s->img, units, k);
    } else {
        if (rl_cnf_with_units(&s->inst, &BASE, units, k) ||
            sa_build_image(&s->img, s->inst.num_vars, s->inst.num_clauses, s->inst.offsets, s->inst.lits)) {
            fprintf(stderr, "rl_board: image build failed (puzzle %u)\n", p);
            exit(1);
        }
        t3 = rl_cycles();
        bytes = sa_dev_load(&s->dev, &s->img, LEAN_XFER);
    }
    uint64_t t4 = rl_cycles();
    if (bytes == (size_t)-1) {
        fprintf(stderr, "rl_board: over the hardware's variable limit\n");
        exit(1);
    }
    r->nn = t1 - t0;
    r->argmax = t2 - t1;
    r->image = t3 - t2;
    r->xfer = t4 - t3;
    r->xfer_bytes = bytes;
    s->puzzle = p;
}

static void start(slot_t *s)
{
    S.regs[(SA_SHELL_BASE + SA_SHELL_CTRL) / 4] = 1;   /* clear tile counters */
    ioctl(S.fd, SATACCEL_IOC_ARM);
    s->t_start = rl_cycles();
    sa_dev_start(&s->dev, &s->img);
}

/* Wait for the tile, then read and check the result */
static void finish(slot_t *s)
{
    rec_t *r = &R[s->puzzle];
    uint64_t t0 = rl_cycles();
    if (sa_dev_wait(&s->dev)) {
        fprintf(stderr, "rl_board: tile timeout (puzzle %u)\n", s->puzzle);
        exit(1);
    }
    uint64_t t1 = rl_cycles();
    r->tile_busy = sa_dev_counter(&s->dev, SA_SHELL_BUSY);
    r->tile_active = sa_dev_counter(&s->dev, SA_SHELL_MEM_ACTIVE);
    r->tile_stall = sa_dev_counter(&s->dev, SA_SHELL_MEM_STALL);
    r->sat = r->tile_busy;
    r->stall = t1 - t0;                                      /* CPU blocked on the tile */
    uint64_t span = t1 - s->t_start;
    r->sync = span > r->sat ? span - r->sat : 0;             /* start + completion detection */

    int8_t *model = calloc(s->img.num_vars, 1);
    sa_dev_result res;
    r->sat_flag = sa_dev_result_read(&s->dev, model, &res);
    r->decisions = res.decisions;
    r->conflicts = res.backtracks;
    memset(r->grid, 0, RL_CELLS);
    if (r->sat_flag == 1)
        for (uint32_t v = 0; v < 729; v++)
            if (model[v] > 0) r->grid[v / 9] = (int8_t)(v % 9 + 1);
    free(model);
}

static void die(const char *m)
{
    fprintf(stderr, "rl_board: %s\n", m);
    exit(2);
}

int main(int argc, char **argv)
{
    const char *data = "/root/CU_SATACCEL/rl/data", *loadable = "/root/CU_SATACCEL/rl/rl_lenet_ip2.nvdla", *csv = NULL;
    int dbl = 0, lean_clear = 0;
    uint32_t n_req = 0;
    static struct option opts[] = {
        {"mode", 1, 0, 'm'}, {"clear", 1, 0, 'c'}, {"xfer", 1, 0, 'x'}, {"nn", 1, 0, 'k'}, {"n", 1, 0, 'n'},
        {"data", 1, 0, 'd'}, {"loadable", 1, 0, 'l'}, {"csv", 1, 0, 'o'}, {"handoff", 1, 0, 'h'},
        {0, 0, 0, 0}};
    int o;
    while ((o = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (o) {
        case 'm': dbl = !strcmp(optarg, "double"); break;
        case 'c': lean_clear = !strcmp(optarg, "lean"); break;
        case 'x': LEAN_XFER = !strcmp(optarg, "used"); break;
        case 'k': USE_TRACE = !strcmp(optarg, "trace"); break;
        case 'n': n_req = (uint32_t)atoi(optarg); break;
        case 'd': data = optarg; break;
        case 'l': loadable = optarg; break;
        case 'o': csv = optarg; break;
        case 'h': DELTA = !strcmp(optarg, "delta"); break;
        default: die("bad option (see the header comment)");
        }
    }
    char p[4096];
    snprintf(p, sizeof p, "%s/cells.bin", data);
    if (cells_read(&CELLS, p)) die("cannot read cells.bin");
    snprintf(p, sizeof p, "%s/base.cnf", data);
    if (rl_cnf_read_dimacs(&BASE, p)) die("cannot read base.cnf");
    snprintf(p, sizeof p, "%s/reference.bin", data);
    if (rl_reference_read(&REF, p)) die("cannot read reference.bin");
    uint32_t n = n_req && n_req < CELLS.num_puzzles ? n_req : CELLS.num_puzzles;
    R = calloc(n, sizeof(rec_t));

    if (!USE_TRACE && !(NV = rl_nvdla_open(loadable, 0))) die("NVDLA setup failed");

    sa_caps caps;
    sa_config cfg;
    sa_hw_caps(&caps);
    sa_default_config(&cfg);
    if (sa_linux_open(&S, &caps)) die("cannot open /dev/sataccel (insmod sataccel.ko?)");
    slot_t slot[2];
    memset(slot, 0, sizeof slot);
    size_t need = sa_dev_mem_required(&caps);
    for (int i = 0; i < (dbl ? 2 : 1); i++) {
        if (sa_image_init(&slot[i].img, &caps, &cfg)) die("out of memory");
        slot[i].img.lean_clear = lean_clear;
        if (i == 0) {
            slot[0].dev = S.dev;
        } else {
            if (S.mem_size < 2 * need) die("DMA buffer too small for two buffer sets (sataccel.ko mem_size)");
            memset(S.mem + need, 0, need);
            if (sa_dev_init(&slot[1].dev, &S.dev.hw, S.mem_phys + need, S.mem + need, need, &caps))
                die("second buffer set");
        }
    }
    /* Delta handoff: each buffer set holds the base formula's image (loaded
     * once, outside the timed run); puzzles only write their decisions */
    uint64_t base_load = 0;
    if (DELTA)
        for (int i = 0; i < (dbl ? 2 : 1); i++) {
            uint64_t t0 = rl_cycles();
            if (sa_build_image(&slot[i].img, BASE.num_vars, BASE.num_clauses, BASE.offsets, BASE.lits) ||
                sa_dev_load(&slot[i].dev, &slot[i].img, LEAN_XFER) == (size_t)-1 || slot[i].img.decision_count)
                die("base image");
            base_load += rl_cycles() - t0;
        }
    double cpu = rl_cycles_per_us();

    uint64_t t_begin = rl_cycles();
    if (!dbl) {
        for (uint32_t k = 0; k < n; k++) {
            prepare(&slot[0], k);
            start(&slot[0]);
            finish(&slot[0]);
        }
    } else {
        prepare(&slot[0], 0);
        start(&slot[0]);
        for (uint32_t k = 1; k < n; k++) {
            slot_t *s = &slot[k % 2], *prev = &slot[(k - 1) % 2];
            prepare(s, k);           /* overlaps the tile solving k-1 */
            finish(prev);
            start(s);
        }
        finish(&slot[(n - 1) % 2]);
    }
    uint64_t wall = rl_cycles() - t_begin;

    const char *mode = dbl ? "double" : "serial";
    const char *clear = lean_clear ? "lean" : "full";
    const char *xfer = LEAN_XFER ? "used" : "allocated";
    uint32_t correct = 0, mism = 0;
    FILE *f = csv ? fopen(csv, "w") : NULL;
    if (csv && !f) die("cannot write csv");
    if (f) {
        fprintf(f, "# mode=%s clear=%s xfer=%s solver=sataccel nn=%s handoff=%s timer=%s cycles_per_us=%.3f "
                   "wall_cycles=%llu puzzles=%u warmup=0 poll_us=0 base_load_cycles=%llu\n",
                mode, clear, xfer, USE_TRACE ? "trace" : "nvdla", DELTA ? "delta" : "full", RL_TIMER_NAME, cpu,
                (unsigned long long)wall, n, (unsigned long long)base_load);
        fprintf(f, "puzzle,givens,nn_cyc,argmax_cyc,image_cyc,xfer_cyc,xfer_bytes,sync_cyc,stall_cyc,sat_cyc,sat,"
                   "decisions,propagations,conflicts,grid,tile_busy,tile_mem_active,tile_mem_stall,nn_mismatch\n");
    }
    for (uint32_t k = 0; k < n; k++) {
        rec_t *r = &R[k];
        int ok = r->sat_flag == rl_reference_sat(&REF, k) &&
                 (r->sat_flag != 1 || !memcmp(r->grid, rl_reference_solved(&REF, k), RL_CELLS));
        correct += ok;
        mism += r->nn_mismatch;
        if (f) {
            char g[RL_CELLS + 1];
            for (int i = 0; i < RL_CELLS; i++) g[i] = (char)('0' + r->grid[i]);
            g[RL_CELLS] = 0;
            fprintf(f, "%u,%u,%llu,%llu,%llu,%llu,%zu,%llu,%llu,%llu,%d,%u,0,%u,%s,%llu,%llu,%llu,%u\n", k, r->givens,
                    (unsigned long long)r->nn, (unsigned long long)r->argmax, (unsigned long long)r->image,
                    (unsigned long long)r->xfer, r->xfer_bytes, (unsigned long long)r->sync,
                    (unsigned long long)r->stall, (unsigned long long)r->sat, r->sat_flag, r->decisions,
                    r->conflicts, g, (unsigned long long)r->tile_busy, (unsigned long long)r->tile_active,
                    (unsigned long long)r->tile_stall, r->nn_mismatch);
        }
    }
    if (f) fclose(f);
    printf("%-6s clear=%-4s xfer=%-9s nn=%-5s handoff=%-5s puzzles=%u  wall %.1f ms (%.1f us/puzzle)  "
           "matches reference %u/%u  NVDLA cells != emulator: %u\n",
           mode, clear, xfer, USE_TRACE ? "trace" : "nvdla", DELTA ? "delta" : "full", n, wall / cpu / 1e3,
           wall / cpu / n, correct, n, mism);
    if (NV) rl_nvdla_close(NV);
    sa_linux_close(&S);
    return 0;
}
