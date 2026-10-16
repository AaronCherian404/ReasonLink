/*
 * P1 baselines: the serial and double-buffered handoff between the neural
 * stage (NVDLA output trace) and the SAT solver, following SAT-Accel's native
 * host flow, with every phase timed by the cycle counter.
 *
 * Host thread (per puzzle):
 *   conv   argmax over the int8 logits -> unit literals (T_conv, argmax part)
 *          base CNF + units -> SAT-Accel memory image  (T_conv, image part)
 *   xfer   copy the image regions into the device buffers (T_xfer)
 *   sync   hand the slot to the device, wait for completion (T_sync)
 * Device thread (stand-in for SAT-Accel, P1 option C):
 *   sat    decode the device copy of the image, solve with MiniSat (T_sat)
 *
 * serial:          one buffer set; the host waits for each solve.
 * double-buffered: two buffer sets with explicit ownership; the host converts
 *                  and transfers puzzle i+1 while the device solves puzzle i.
 *
 * T_nn is not measured in P1: the neural stage is a pre-computed trace in
 * memory (NVDLA runs on ESP after P1).
 *
 * Usage: rl_baselines --mode serial|double [--clear full|lean]
 *          [--xfer allocated|used] [--n N] [--warmup W] [--poll-us U]
 *          [--data DIR] [--csv FILE]
 */
#define _GNU_SOURCE
#include <getopt.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cnf.h"
#include "rl_timer.h"
#include "sataccel_image.h"
#include "solver_backend.h"
#include "trace.h"

enum { SLOT_FREE = 0, SLOT_READY = 1, SLOT_DONE = 2 };

typedef struct {
    sa_image host;                      /* host-side image (conversion target) */
    rl_cnf inst;                        /* base + units scratch */
    uint8_t *dev[SA_NUM_REGIONS];       /* device-side copies of the regions */
    sa_image dev_view;                  /* the device's view of its buffers */
    _Atomic int state;                  /* ownership: FREE/DONE host, READY device */
    /* written by the device */
    rl_solve_result res;
    uint64_t sat_cyc;
    int8_t grid[RL_CELLS];
} slot_t;

typedef struct {
    uint64_t argmax, image, xfer, sync, stall, sat;
    size_t xfer_bytes;
    uint32_t puzzle, givens;
    int sat_flag;
    uint64_t decisions, propagations, conflicts;
    int8_t grid[RL_CELLS];
} rec_t;

typedef struct {
    int dbl, lean_clear, xfer_used, poll_us;
    uint32_t n;
    rl_cnf base;
    rl_trace tr;
    slot_t slot[2];
    int nslots;
    rec_t *rec;
} ctx_t;

static void die(const char *msg)
{
    fprintf(stderr, "rl_baselines: %s\n", msg);
    exit(1);
}

static size_t region_capacity(const sa_caps *c, int r)
{
    static const size_t per[SA_NUM_REGIONS] = {4, 4, 8, 4, 4, SA_LMD_BYTES, 8, 4};
    size_t n[SA_NUM_REGIONS] = {c->max_clause_elements, SA_TRACK_LBD_WORDS, c->max_clauses,
                                c->max_literal_elements, c->max_vars, c->max_vars,
                                c->max_clauses, 256};
    return n[r] * per[r];
}

static void slot_init(slot_t *s, int lean_clear)
{
    sa_caps caps;
    sa_config cfg;
    sa_default_caps(&caps);
    sa_default_config(&cfg);
    memset(s, 0, sizeof(*s));
    if (sa_image_init(&s->host, &caps, &cfg)) die("out of memory (host image)");
    s->host.lean_clear = lean_clear;
    for (int r = 0; r < SA_NUM_REGIONS; r++)
        if (!(s->dev[r] = calloc(1, region_capacity(&caps, r)))) die("out of memory (device)");
    s->dev_view = s->host;               /* caps/config; pointers replaced below */
    s->dev_view.clause_store = (int32_t *)s->dev[0];
    s->dev_view.cmd = (uint32_t *)s->dev[2];
    s->dev_view.lit_store = (int32_t *)s->dev[3];
    s->dev_view.answer_stack = (int32_t *)s->dev[4];
    s->dev_view.lmd = s->dev[5];
    s->dev_view.cls_states = (int32_t *)s->dev[6];
    s->dev_view.scratch = NULL;
    s->dev_view.scratch_cap = 0;
    atomic_init(&s->state, SLOT_FREE);
}

/* T_conv: argmax + image build into the slot's host image. */
static void convert(ctx_t *c, slot_t *s, uint32_t p, rec_t *r)
{
    int32_t units[RL_CELLS];
    uint64_t t0 = rl_cycles();
    uint32_t k = rl_argmax_units(&c->tr, p, units);
    uint64_t t1 = rl_cycles();
    if (rl_cnf_with_units(&s->inst, &c->base, units, k) ||
        sa_build_image(&s->host, s->inst.num_vars, s->inst.num_clauses, s->inst.offsets, s->inst.lits))
        die("image build failed (capacity)");
    uint64_t t2 = rl_cycles();
    r->argmax = t1 - t0;
    r->image = t2 - t1;
    r->givens = k;
    r->puzzle = p;
}

/* T_xfer: copy regions host -> device (allocated sizes, as host.cpp migrates,
 * or only the used prefix), then the scalar kernel arguments. */
static void transfer(ctx_t *c, slot_t *s, rec_t *r)
{
    sa_region rows[SA_NUM_REGIONS];
    uint64_t t0 = rl_cycles();
    sa_regions(&s->host, rows);
    size_t total = 0;
    for (int i = 0; i < SA_NUM_REGIONS; i++) {
        size_t n = c->xfer_used ? rows[i].used : rows[i].allocated;
        memcpy(s->dev[i], sa_region_ptr(&s->host, i), n);
        total += n;
    }
    s->dev_view.num_vars = s->host.num_vars;
    s->dev_view.num_clauses = s->host.num_clauses;
    s->dev_view.clause_elements = s->host.clause_elements;
    s->dev_view.literal_elements = s->host.literal_elements;
    s->dev_view.decision_count = s->host.decision_count;
    r->xfer = rl_cycles() - t0;
    r->xfer_bytes = total;
}

static void collect(slot_t *s, rec_t *r)
{
    r->sat = s->sat_cyc;
    r->sat_flag = s->res.sat;
    r->decisions = s->res.decisions;
    r->propagations = s->res.propagations;
    r->conflicts = s->res.conflicts;
    memcpy(r->grid, s->grid, RL_CELLS);
    atomic_store_explicit(&s->state, SLOT_FREE, memory_order_release);
}

static void wait_not(ctx_t *c, slot_t *s, int st)
{
    while (atomic_load_explicit(&s->state, memory_order_acquire) == st)
        if (c->poll_us) usleep((useconds_t)c->poll_us); else rl_cpu_relax();
}

/* Device: solve puzzles in order, slot k % nslots. */
static void *device_main(void *arg)
{
    ctx_t *c = arg;
    int8_t *model = malloc(c->base.num_vars);
    for (uint32_t k = 0; k < c->n; k++) {
        slot_t *s = &c->slot[k % c->nslots];
        while (atomic_load_explicit(&s->state, memory_order_acquire) != SLOT_READY)
            rl_cpu_relax();
        uint64_t t0 = rl_cycles();
        s->res = rl_solve_image(&s->dev_view, model);
        memset(s->grid, 0, RL_CELLS);
        if (s->res.sat == 1)
            for (uint32_t v = 0; v < c->base.num_vars && v < 729; v++)
                if (model[v] > 0) s->grid[v / 9] = (int8_t)(v % 9 + 1);
        s->sat_cyc = rl_cycles() - t0;
        atomic_store_explicit(&s->state, SLOT_DONE, memory_order_release);
    }
    free(model);
    return NULL;
}

/* Runs puzzles [0, n); returns wall-clock cycles for the whole run. */
static uint64_t run(ctx_t *c, uint32_t n)
{
    pthread_t dev;
    c->n = n;
    memset(c->rec, 0, n * sizeof(rec_t));
    if (pthread_create(&dev, NULL, device_main, c)) die("pthread_create");
    uint64_t t_begin = rl_cycles();
    if (!c->dbl) {
        slot_t *s = &c->slot[0];
        for (uint32_t k = 0; k < n; k++) {
            rec_t *r = &c->rec[k];
            convert(c, s, k, r);
            transfer(c, s, r);
            uint64_t t0 = rl_cycles();
            atomic_store_explicit(&s->state, SLOT_READY, memory_order_release);
            wait_not(c, s, SLOT_READY);
            uint64_t t1 = rl_cycles();
            collect(s, r);
            r->sync = (t1 - t0) - r->sat;          /* doorbell + completion detection */
        }
    } else {
        for (uint32_t k = 0; k < n; k++) {
            slot_t *s = &c->slot[k % 2];
            rec_t *r = &c->rec[k];
            uint64_t t0 = rl_cycles();
            wait_not(c, s, SLOT_READY);            /* device still owns this buffer set */
            if (k >= 2) collect(s, &c->rec[k - 2]);
            r->stall = rl_cycles() - t0;
            convert(c, s, k, r);
            transfer(c, s, r);
            uint64_t t1 = rl_cycles();
            atomic_store_explicit(&s->state, SLOT_READY, memory_order_release);
            r->sync = rl_cycles() - t1;
        }
        for (uint32_t k = n > 2 ? n - 2 : 0; k < n; k++) {
            slot_t *s = &c->slot[k % 2];
            wait_not(c, s, SLOT_READY);
            collect(s, &c->rec[k]);
        }
    }
    uint64_t wall = rl_cycles() - t_begin;
    pthread_join(dev, NULL);
    return wall;
}

int main(int argc, char **argv)
{
    ctx_t c;
    memset(&c, 0, sizeof c);
    const char *data = "../data/sudoku", *csv = NULL;
    uint32_t n_req = 0, warmup = 20;
    static struct option opts[] = {
        {"mode", 1, 0, 'm'}, {"clear", 1, 0, 'c'}, {"xfer", 1, 0, 'x'}, {"n", 1, 0, 'n'},
        {"warmup", 1, 0, 'w'}, {"poll-us", 1, 0, 'p'}, {"data", 1, 0, 'd'}, {"csv", 1, 0, 'o'},
        {0, 0, 0, 0}};
    int o;
    while ((o = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (o) {
        case 'm': c.dbl = !strcmp(optarg, "double"); break;
        case 'c': c.lean_clear = !strcmp(optarg, "lean"); break;
        case 'x': c.xfer_used = !strcmp(optarg, "used"); break;
        case 'n': n_req = (uint32_t)atoi(optarg); break;
        case 'w': warmup = (uint32_t)atoi(optarg); break;
        case 'p': c.poll_us = atoi(optarg); break;
        case 'd': data = optarg; break;
        case 'o': csv = optarg; break;
        default: die("bad option (see header comment for usage)");
        }
    }
    char path[4096];
    snprintf(path, sizeof path, "%s/base.cnf", data);
    if (rl_cnf_read_dimacs(&c.base, path)) die("cannot read base.cnf");
    snprintf(path, sizeof path, "%s/trace.bin", data);
    if (rl_trace_read(&c.tr, path)) die("cannot read trace.bin");

    uint32_t n = n_req && n_req < c.tr.num_puzzles ? n_req : c.tr.num_puzzles;
    c.nslots = c.dbl ? 2 : 1;
    for (int i = 0; i < c.nslots; i++) slot_init(&c.slot[i], c.lean_clear);
    c.rec = calloc(n, sizeof(rec_t));
    double cyc_per_us = rl_cycles_per_us();

    if (warmup) run(&c, warmup < n ? warmup : n);      /* warm caches, page in buffers */
    uint64_t wall = run(&c, n);

    const char *mode = c.dbl ? "double" : "serial";
    const char *clear = c.lean_clear ? "lean" : "full";
    const char *xfer = c.xfer_used ? "used" : "allocated";
    FILE *f = csv ? fopen(csv, "w") : NULL;
    if (csv && !f) die("cannot write csv");
    if (f) {
        fprintf(f, "# mode=%s clear=%s xfer=%s timer=%s cycles_per_us=%.3f wall_cycles=%llu "
                   "puzzles=%u warmup=%u poll_us=%d\n",
                mode, clear, xfer, RL_TIMER_NAME, cyc_per_us, (unsigned long long)wall, n, warmup,
                c.poll_us);
        fprintf(f, "puzzle,givens,argmax_cyc,image_cyc,xfer_cyc,xfer_bytes,sync_cyc,stall_cyc,"
                   "sat_cyc,sat,decisions,propagations,conflicts,grid\n");
        for (uint32_t k = 0; k < n; k++) {
            rec_t *r = &c.rec[k];
            char g[RL_CELLS + 1];
            for (int i = 0; i < RL_CELLS; i++) g[i] = (char)('0' + r->grid[i]);
            g[RL_CELLS] = 0;
            fprintf(f, "%u,%u,%llu,%llu,%llu,%zu,%llu,%llu,%llu,%d,%llu,%llu,%llu,%s\n", r->puzzle,
                    r->givens, (unsigned long long)r->argmax, (unsigned long long)r->image,
                    (unsigned long long)r->xfer, r->xfer_bytes, (unsigned long long)r->sync,
                    (unsigned long long)r->stall, (unsigned long long)r->sat, r->sat_flag,
                    (unsigned long long)r->decisions, (unsigned long long)r->propagations,
                    (unsigned long long)r->conflicts, g);
        }
        fclose(f);
    }
    uint64_t sum_sat = 0;
    for (uint32_t k = 0; k < n; k++) sum_sat += c.rec[k].sat;
    printf("%-6s clear=%-4s xfer=%-9s puzzles=%u  wall %.1f ms  (%.1f us/puzzle, solver %.1f us/puzzle)\n",
           mode, clear, xfer, n, wall / cyc_per_us / 1e3, wall / cyc_per_us / n,
           sum_sat / cyc_per_us / n);
    return 0;
}
