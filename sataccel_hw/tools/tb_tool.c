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
 *   mem.txt   device memory after the load ("addr data" 64-bit words, non-zero only)
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
static uint32_t rec_rd32(void *c, uint32_t off) { (void)c; (void)off; return 0; }
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
    for (size_t a = 0; a + 8 <= size; a += 8) {
        uint64_t w;
        memcpy(&w, shadow + a, 8);
        if (w) {
            fprintf(m, "%08llx %016llx\n", (unsigned long long)(SA_HW_MEM_BASE + a), (unsigned long long)w);
            words++;
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
    printf("%s: %u vars, %u clauses, %zu non-zero memory words\n", out, inst->num_vars, inst->num_clauses, words);
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
    if (argc == 5 && !strcmp(argv[1], "gen-cnf")) {
        rl_cnf c;
        if (rl_cnf_read_dimacs(&c, argv[2])) die("cannot read cnf");
        return gen(&c, atoi(argv[3]), NULL, argv[4]);
    }
    if (argc == 3 && !strcmp(argv[1], "check")) return check(argv[2]);
    fprintf(stderr, "usage: tb_tool gen-sudoku <data_dir> <puzzle> <out_dir>\n"
                    "       tb_tool gen-cnf <file.cnf> <1|0> <out_dir>\n"
                    "       tb_tool check <out_dir>\n");
    return 2;
}
