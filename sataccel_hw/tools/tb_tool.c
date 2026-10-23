/*
 * Stimulus and checking for the SAT-Accel RTL testbench (tb/tb_sataccel.sv).
 *
 *   tb_tool gen-sudoku <data_dir> <puzzle> <out_dir>
 *   tb_tool gen-cnf <file.cnf> <expect 1|0> <out_dir>
 *   tb_tool check <out_dir>
 *
 * gen-*: build the SAT-Accel image for the instance (our byte-exact port of
 * host.cpp's conversion, hardware capacities), then run the real driver core
 * (drv/sataccel_drv.c) with a record backend. Writes to out_dir:
 *   ops.txt   register accesses for the testbench to replay over APB
 *   mem.txt   device memory after the load ("addr data" 64-bit words): by default
 *             the non-zero words of a faithful load; with TB_MEM=used, every
 *             word of each region's used prefix (zeros included) and nothing
 *             else, to run against a garbage-filled memory (+fill)
 *   inst.cnf  the instance (base formula + predicted givens)
 *   meta.txt  num_vars, expected SAT/UNSAT, reference grid (Sudoku)
 * check: read the testbench's dump.txt back through the same driver core
 * (sa_dev_result_read), check the model against every clause, and for
 * Sudoku compare with the PySAT reference grid.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cnf.h"
#include "sataccel_caps.h"
#include "sataccel_drv.h"
#include "sataccel_image.h"
#include "trace.h"

static void die(const char *m)
{
    fprintf(stderr, "tb_tool: %s\n", m);
    exit(2);
}

/* ---- record backend ---- */
typedef struct { FILE *ops; } rec_ctx;

static void rec_wr32(void *c, uint32_t off, uint32_t v) { fprintf(((rec_ctx *)c)->ops, "W %05x %08x\n", off, v); }
/* Reads are not replayed (polls are): answer what the hardware would, so the
 * driver's ID checks pass; status reads see "no error". */
static uint32_t rec_rd32(void *c, uint32_t off)
{
    (void)c;
    return off == SA_RL_BASE ? SA_RL_ID_VALUE : 0;
}
static int rec_poll(void *c, uint32_t off, uint32_t mask, uint32_t val)
{
    fprintf(((rec_ctx *)c)->ops, "P %05x %08x %08x\n", off, mask, val);
    return 0;
}
static void rec_read_back(void *c, uint64_t addr, void *dst, size_t len)
{
    (void)dst;
    fprintf(((rec_ctx *)c)->ops, "D %08llx %zx\n", (unsigned long long)addr, len);
}

static FILE *open_in(const char *dir, const char *name, const char *mode)
{
    char p[4096];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, mode);
    if (!f) { fprintf(stderr, "tb_tool: cannot open %s\n", p); exit(2); }
    return f;
}

static void dev_setup(sa_dev *d, sa_hw_ops *ops, rec_ctx *rc, uint8_t **shadow, size_t *size)
{
    sa_caps caps;
    sa_hw_caps(&caps);
    *size = sa_dev_mem_required(&caps);
    *shadow = calloc(1, *size);
    if (!*shadow) die("out of memory");
    memset(ops, 0, sizeof *ops);
    ops->wr32 = rec_wr32;
    ops->rd32 = rec_rd32;
    ops->poll = rec_poll;
    ops->mem_read_back = rec_read_back;
    ops->ctx = rc;
    if (sa_dev_init(d, ops, SA_HW_MEM_BASE, *shadow, *size, &caps)) die("sa_dev_init");
}

static void write_cnf(FILE *f, const rl_cnf *c)
{
    fprintf(f, "p cnf %u %u\n", c->num_vars, c->num_clauses);
    for (uint32_t i = 0; i < c->num_clauses; i++) {
        for (uint32_t k = c->offsets[i]; k < c->offsets[i + 1]; k++) fprintf(f, "%d ", c->lits[k]);
        fprintf(f, "0\n");
    }
}

static int gen(const rl_cnf *inst, int expect, const int8_t *ref_grid, const char *out)
{
    sa_caps caps;
    sa_config cfg;
    sa_image img;
    sa_hw_caps(&caps);
    sa_default_config(&cfg);
    if (sa_image_init(&img, &caps, &cfg)) die("sa_image_init");
    if (sa_build_image(&img, inst->num_vars, inst->num_clauses, inst->offsets, inst->lits))
        die("image build failed (capacity)");

    rec_ctx rc;
    rc.ops = open_in(out, "ops.txt", "w");
    sa_dev d;
    sa_hw_ops ops;
    uint8_t *shadow;
    size_t size;
    dev_setup(&d, &ops, &rc, &shadow, &size);

    size_t bytes = sa_dev_load(&d, &img, 0);   /* faithful: allocated sizes, as host.cpp */
    if (bytes == (size_t)-1) die("instance exceeds the hardware's variable limit (max_vars / 2)");
    fprintf(rc.ops, "# image: %u vars, %u clauses, %zu bytes loaded\n", img.num_vars, img.num_clauses, bytes);
    fprintf(rc.ops, "W %05x 00000001\n", SA_SHELL_BASE + SA_SHELL_CTRL);   /* clear counters */
    sa_dev_start(&d, &img);
    sa_dev_wait(&d);
    fprintf(rc.ops, "C done\n");
    fclose(rc.ops);

    FILE *m = open_in(out, "mem.txt", "w");
    size_t words = 0;
    const char *mode = getenv("TB_MEM");
    if (mode && !strcmp(mode, "used")) {
        /* Used prefix of each image region, in its device buffer (sa_dev_load's mapping) */
        static const int dst[SA_NUM_REGIONS] = {
            SA_BUF_CLAUSE_STORE, SA_BUF_TRACK_LBD, SA_BUF_CMD, SA_BUF_LIT_STORE,
            SA_BUF_ANSWER_STACK, SA_BUF_LMD, SA_BUF_CLS_STATES, SA_BUF_MISC};
        sa_region rows[SA_NUM_REGIONS];
        sa_regions(&img, rows);
        for (int r = 0; r < SA_NUM_REGIONS; r++) {
            size_t end = d.buf_off[dst[r]] + ((rows[r].used + 7) & ~(size_t)7);
            for (size_t a = d.buf_off[dst[r]]; a < end; a += 8) {
                uint64_t w;
                memcpy(&w, shadow + a, 8);
                fprintf(m, "%08llx %016llx\n", (unsigned long long)(SA_HW_MEM_BASE + a), (unsigned long long)w);
                words++;
            }
        }
    } else {
        for (size_t a = 0; a + 8 <= size; a += 8) {
            uint64_t w;
            memcpy(&w, shadow + a, 8);
            if (w) {
                fprintf(m, "%08llx %016llx\n", (unsigned long long)(SA_HW_MEM_BASE + a), (unsigned long long)w);
                words++;
            }
        }
    }
    fclose(m);

    FILE *c = open_in(out, "inst.cnf", "w");
    write_cnf(c, inst);
    fclose(c);
    FILE *meta = open_in(out, "meta.txt", "w");
    fprintf(meta, "%u %d %d\n", inst->num_vars, expect, ref_grid != NULL);
    if (ref_grid)
        for (int i = 0; i < RL_CELLS; i++) fprintf(meta, "%d%c", ref_grid[i], i + 1 == RL_CELLS ? '\n' : ' ');
    fclose(meta);
    printf("%s: %u vars, %u clauses, %zu memory words\n", out, inst->num_vars, inst->num_clauses, words);
    return 0;
}

/* ReasonLink test: the base formula's image is loaded once (faithful); the
 * emulator's int8 logits for each given cell sit in memory where NVDLA would
 * have written them (group-32 layout, 64 B per cell); the recorded program
 * is ReasonLink's (setup, begin, one push per given cell, commit), then the
 * usual SAT-Accel start/wait. The checker sees the full instance (base +
 * givens), so the result is checked exactly as for gen-sudoku. */
#define RL_LOGIT_BASE (SA_HW_MEM_BASE + 0x2000000u)

static int gen_rl(const rl_cnf *base, const rl_trace *tr, uint32_t k, int expect, const int8_t *ref_grid,
                  const char *out)
{
    sa_caps caps;
    sa_config cfg;
    sa_image img;
    sa_hw_caps(&caps);
    sa_default_config(&cfg);
    if (sa_image_init(&img, &caps, &cfg) ||
        sa_build_image(&img, base->num_vars, base->num_clauses, base->offsets, base->lits) || img.decision_count)
        die("base image");

    rec_ctx rc;
    rc.ops = open_in(out, "ops.txt", "w");
    sa_dev d;
    sa_hw_ops ops;
    uint8_t *shadow;
    size_t size;
    dev_setup(&d, &ops, &rc, &shadow, &size);
    if (sa_dev_load(&d, &img, 0) == (size_t)-1) die("base image too large");

    const uint8_t *mask = rl_trace_mask(tr, k);
    const int8_t *scores = rl_trace_scores(tr, k);
    fprintf(rc.ops, "# ReasonLink: base image (%u vars, %u clauses) + puzzle %u\n", img.num_vars, img.num_clauses, k);
    fprintf(rc.ops, "W %05x 00000001\n", SA_SHELL_BASE + SA_SHELL_CTRL);   /* clear counters */
    if (sa_rl_setup(&d, &img, 32, 0)) die("sa_rl_setup");
    sa_rl_begin(&d);
    uint32_t givens = 0;
    for (uint32_t cell = 0; cell < RL_CELLS; cell++)
        if (mask[cell]) {
            sa_rl_push(&d, RL_LOGIT_BASE + 64 * cell, cell);
            givens++;
        }
    if (sa_rl_commit(&d)) die("sa_rl_commit");
    fprintf(rc.ops, "R %05x\nR %05x\nR %05x\n", SA_RL_BASE + 0x24, SA_RL_BASE + 0x28, SA_RL_BASE + 0x2C);
    fprintf(rc.ops, "C reasonlink\n");
    sa_dev_start(&d, &img);
    sa_dev_wait(&d);
    fprintf(rc.ops, "C done\n");
    fclose(rc.ops);

    FILE *m = open_in(out, "mem.txt", "w");
    size_t words = 0;
    for (size_t a = 0; a + 8 <= size; a += 8) {
        uint64_t w;
        memcpy(&w, shadow + a, 8);
        if (w) {
            fprintf(m, "%08llx %016llx\n", (unsigned long long)(SA_HW_MEM_BASE + a), (unsigned long long)w);
            words++;
        }
    }
    for (uint32_t cell = 0; cell < RL_CELLS; cell++)       /* NVDLA's output for each given cell */
        if (mask[cell])
            for (int h = 0; h < 2; h++) {
                uint64_t w = 0;
                for (int b = 0; b < 8; b++) {
                    int c = 8 * h + b;
                    if (c < 10) w |= (uint64_t)(uint8_t)scores[(size_t)cell * tr->stride + c] << (8 * b);
                }
                fprintf(m, "%08llx %016llx\n", (unsigned long long)(RL_LOGIT_BASE + 64 * cell + 8 * h),
                        (unsigned long long)w);
            }
    fclose(m);

    /* the full instance, for the checker */
    int32_t units[RL_CELLS];
    uint32_t u = rl_argmax_units(tr, k, units);
    rl_cnf inst;
    memset(&inst, 0, sizeof inst);
    if (rl_cnf_with_units(&inst, base, units, u)) die("rl_cnf_with_units");
    FILE *c = open_in(out, "inst.cnf", "w");
    write_cnf(c, &inst);
    fclose(c);
    FILE *meta = open_in(out, "meta.txt", "w");
    fprintf(meta, "%u %d %d\n", inst.num_vars, expect, 1);
    for (int i = 0; i < RL_CELLS; i++) fprintf(meta, "%d%c", ref_grid[i], i + 1 == RL_CELLS ? '\n' : ' ');
    fclose(meta);
    printf("%s: ReasonLink, %u givens, %zu memory words\n", out, givens, words);
    return 0;
}

static int check(const char *out)
{
    rl_cnf inst;
    char p[4096];
    snprintf(p, sizeof p, "%s/inst.cnf", out);
    if (rl_cnf_read_dimacs(&inst, p)) die("cannot read inst.cnf");
    unsigned nv;
    int expect, has_grid;
    int8_t ref_grid[RL_CELLS] = {0};
    FILE *meta = open_in(out, "meta.txt", "r");
    if (fscanf(meta, "%u %d %d", &nv, &expect, &has_grid) != 3) die("bad meta.txt");
    for (int i = 0; has_grid && i < RL_CELLS; i++) {
        int v;
        if (fscanf(meta, "%d", &v) != 1) die("bad meta.txt grid");
        ref_grid[i] = (int8_t)v;
    }
    fclose(meta);

    rec_ctx rc = {NULL};
    sa_dev d;
    sa_hw_ops ops;
    uint8_t *shadow;
    size_t size;
    dev_setup(&d, &ops, &rc, &shadow, &size);
    d.num_vars = nv;

    FILE *dump = open_in(out, "dump.txt", "r");
    unsigned long long a, w;
    while (fscanf(dump, "%llx %llx", &a, &w) == 2)
        if (a >= SA_HW_MEM_BASE && a - SA_HW_MEM_BASE + 8 <= size) memcpy(shadow + (a - SA_HW_MEM_BASE), &w, 8);
    fclose(dump);

    int8_t *model = calloc(nv, 1);
    sa_dev_result r;
    int st = sa_dev_result_read(&d, model, &r);
    int model_ok = 1;
    if (st == 1)
        for (uint32_t i = 0; i < inst.num_clauses && model_ok; i++) {
            int sat = 0;
            for (uint32_t k = inst.offsets[i]; k < inst.offsets[i + 1] && !sat; k++) {
                int32_t l = inst.lits[k];
                sat = model[(l < 0 ? -l : l) - 1] == (l > 0 ? 1 : -1);
            }
            model_ok = sat;
        }
    int grid_ok = 1;
    if (has_grid && st == 1)
        for (uint32_t v = 0; v < 729; v++)
            if (model[v] > 0 && ref_grid[v / 9] != (int8_t)(v % 9 + 1)) grid_ok = 0;
    int ok = st == expect && model_ok;
    printf("%s %s result=%s expect=%s model_ok=%d grid==ref=%s iterations=%u decisions=%u conflicts=%u restarts=%u\n",
           ok ? "PASS" : "FAIL", out, st == 1 ? "SAT" : st == 0 ? "UNSAT" : "ERROR", expect ? "SAT" : "UNSAT",
           model_ok, has_grid ? (grid_ok ? "yes" : "no(alternative?)") : "n/a", r.iterations, r.decisions,
           r.backtracks, r.restarts);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 5 && !strcmp(argv[1], "gen-sudoku")) {
        char p[4096];
        rl_cnf base, inst;
        rl_trace tr;
        rl_reference ref;
        memset(&inst, 0, sizeof inst);
        snprintf(p, sizeof p, "%s/base.cnf", argv[2]);
        if (rl_cnf_read_dimacs(&base, p)) die("cannot read base.cnf");
        snprintf(p, sizeof p, "%s/trace.bin", argv[2]);
        if (rl_trace_read(&tr, p)) die("cannot read trace.bin");
        snprintf(p, sizeof p, "%s/reference.bin", argv[2]);
        if (rl_reference_read(&ref, p)) die("cannot read reference.bin");
        uint32_t k = (uint32_t)atoi(argv[3]);
        if (k >= tr.num_puzzles) die("puzzle out of range");
        int32_t units[RL_CELLS];
        uint32_t n = rl_argmax_units(&tr, k, units);
        if (rl_cnf_with_units(&inst, &base, units, n)) die("rl_cnf_with_units");
        return gen(&inst, rl_reference_sat(&ref, k), rl_reference_solved(&ref, k), argv[4]);
    }
    if (argc == 5 && !strcmp(argv[1], "gen-rl-sudoku")) {
        char p[4096];
        rl_cnf base;
        rl_trace tr;
        rl_reference ref;
        snprintf(p, sizeof p, "%s/base.cnf", argv[2]);
        if (rl_cnf_read_dimacs(&base, p)) die("cannot read base.cnf");
        snprintf(p, sizeof p, "%s/trace.bin", argv[2]);
        if (rl_trace_read(&tr, p)) die("cannot read trace.bin");
        snprintf(p, sizeof p, "%s/reference.bin", argv[2]);
        if (rl_reference_read(&ref, p)) die("cannot read reference.bin");
        uint32_t k = (uint32_t)atoi(argv[3]);
        if (k >= tr.num_puzzles) die("puzzle out of range");
        return gen_rl(&base, &tr, k, rl_reference_sat(&ref, k), rl_reference_solved(&ref, k), argv[4]);
    }
    if (argc == 5 && !strcmp(argv[1], "gen-cnf")) {
        rl_cnf c;
        if (rl_cnf_read_dimacs(&c, argv[2])) die("cannot read cnf");
        return gen(&c, atoi(argv[3]), NULL, argv[4]);
    }
    if (argc == 3 && !strcmp(argv[1], "check")) return check(argv[2]);
    fprintf(stderr, "usage: tb_tool gen-sudoku <data_dir> <puzzle> <out_dir>\n"
                    "       tb_tool gen-rl-sudoku <data_dir> <puzzle> <out_dir>   (ReasonLink)\n"
                    "       tb_tool gen-cnf <file.cnf> <1|0> <out_dir>\n"
                    "       tb_tool check <out_dir>\n");
    return 2;
}
