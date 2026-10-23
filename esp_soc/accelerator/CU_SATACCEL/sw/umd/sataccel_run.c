/*
 * sataccel_run: solve instances on the SAT-Accel tile from Linux on Ariane.
 *
 *   sataccel_run cnf <file.cnf> <expect 1|0> [--lean]
 *   sataccel_run sudoku <data_dir> <first> <count> [--lean]
 *
 * Builds the SAT-Accel image on the CPU (our port of host.cpp's conversion),
 * loads it into the tile's DMA memory (faithful: allocated sizes, or --lean:
 * used prefixes), runs the tile and checks the answer (every clause; Sudoku
 * also against the PySAT reference). Prints CPU cycles for build, load and
 * solve, plus the tile's own counters (busy, memory active/stall cycles).
 * This is the bring-up test; the baselines (Stage 5) build on libsataccel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "cnf.h"
#include "sataccel_ioctl.h"
#include "rl_timer.h"
#include "sataccel_caps.h"
#include "sataccel_linux.h"
#include "trace.h"

static int satisfies(const rl_cnf *c, const int8_t *m)
{
    for (uint32_t i = 0; i < c->num_clauses; i++) {
        int sat = 0;
        for (uint32_t k = c->offsets[i]; k < c->offsets[i + 1] && !sat; k++) {
            int32_t l = c->lits[k];
            sat = m[(l < 0 ? -l : l) - 1] == (l > 0 ? 1 : -1);
        }
        if (!sat) return 0;
    }
    return 1;
}

static sa_linux S;
static sa_image IMG;

/* Solve one instance; returns 1 if correct. */
static int run_one(const char *name, const rl_cnf *c, int expect, const int8_t *ref_grid, int lean)
{
    uint64_t t0 = rl_cycles();
    if (sa_build_image(&IMG, c->num_vars, c->num_clauses, c->offsets, c->lits)) {
        printf("%s: image build failed (capacity)\n", name);
        return 0;
    }
    uint64_t t1 = rl_cycles();
    size_t bytes = sa_dev_load(&S.dev, &IMG, lean);                    /* T_xfer */
    uint64_t t2 = rl_cycles();
    if (bytes == (size_t)-1) {
        printf("%s: over the hardware's variable limit\n", name);
        return 0;
    }
    S.regs[(SA_SHELL_BASE + SA_SHELL_CTRL) / 4] = 1;                  /* clear tile counters */
    ioctl(S.fd, SATACCEL_IOC_ARM);
    sa_dev_start(&S.dev, &IMG);
    int rc = sa_dev_wait(&S.dev);                                      /* T_sync + T_sat */
    uint64_t t3 = rl_cycles();
    if (rc) printf("%s: timeout waiting for the tile\n", name);
    int8_t *model = calloc(c->num_vars, 1);
    sa_dev_result r;
    int st = sa_dev_result_read(&S.dev, model, &r);
    int ok = st == expect && (st != 1 || satisfies(c, model));
    int grid_ok = -1;
    if (ref_grid && st == 1) {
        grid_ok = 1;
        for (uint32_t v = 0; v < 729; v++)
            if (model[v] > 0 && ref_grid[v / 9] != (int8_t)(v % 9 + 1)) grid_ok = 0;
    }
    printf("%-5s %s result=%s expect=%s grid==ref=%s decisions=%u conflicts=%u restarts=%u "
           "build_cyc=%llu load_cyc=%llu solve_cyc=%llu bytes=%zu tile_busy=%llu tile_mem_active=%llu "
           "tile_mem_stall=%llu\n",
           ok ? "PASS" : "FAIL", name, st == 1 ? "SAT" : st == 0 ? "UNSAT" : "ERROR", expect ? "SAT" : "UNSAT",
           grid_ok < 0 ? "n/a" : grid_ok ? "yes" : "no", r.decisions, r.backtracks, r.restarts,
           (unsigned long long)(t1 - t0), (unsigned long long)(t2 - t1), (unsigned long long)(t3 - t2), bytes,
           (unsigned long long)sa_dev_counter(&S.dev, SA_SHELL_BUSY),
           (unsigned long long)sa_dev_counter(&S.dev, SA_SHELL_MEM_ACTIVE),
           (unsigned long long)sa_dev_counter(&S.dev, SA_SHELL_MEM_STALL));
    free(model);
    return ok;
}

int main(int argc, char **argv)
{
    int lean = argc > 1 && !strcmp(argv[argc - 1], "--lean");
    if (lean) argc--;
    sa_caps caps;
    sa_config cfg;
    sa_hw_caps(&caps);
    sa_default_config(&cfg);
    if (sa_image_init(&IMG, &caps, &cfg)) { fprintf(stderr, "out of memory\n"); return 2; }
    IMG.lean_clear = lean;
    if (sa_linux_open(&S, &caps)) { perror("sataccel: open /dev/sataccel"); return 2; }

    int fails = 0, n = 0;
    if (argc == 4 && !strcmp(argv[1], "cnf")) {
        rl_cnf c;
        if (rl_cnf_read_dimacs(&c, argv[2])) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
        n = 1;
        fails = !run_one(argv[2], &c, atoi(argv[3]), NULL, lean);
    } else if (argc == 5 && !strcmp(argv[1], "sudoku")) {
        char p[4096];
        rl_cnf base, inst;
        rl_trace tr;
        rl_reference ref;
        memset(&inst, 0, sizeof inst);
        snprintf(p, sizeof p, "%s/base.cnf", argv[2]);
        if (rl_cnf_read_dimacs(&base, p)) { fprintf(stderr, "cannot read %s\n", p); return 2; }
        snprintf(p, sizeof p, "%s/trace.bin", argv[2]);
        if (rl_trace_read(&tr, p)) { fprintf(stderr, "cannot read %s\n", p); return 2; }
        snprintf(p, sizeof p, "%s/reference.bin", argv[2]);
        if (rl_reference_read(&ref, p)) { fprintf(stderr, "cannot read %s\n", p); return 2; }
        uint32_t first = (uint32_t)atoi(argv[3]), count = (uint32_t)atoi(argv[4]);
        for (uint32_t k = first; k < first + count && k < tr.num_puzzles; k++, n++) {
            int32_t units[RL_CELLS];
            uint32_t u = rl_argmax_units(&tr, k, units);
            if (rl_cnf_with_units(&inst, &base, units, u)) return 2;
            snprintf(p, sizeof p, "sudoku_%u", k);
            fails += !run_one(p, &inst, rl_reference_sat(&ref, k), rl_reference_solved(&ref, k), lean);
        }
    } else {
        fprintf(stderr, "usage: %s cnf <file.cnf> <1|0> [--lean]\n       %s sudoku <data_dir> <first> <count> [--lean]\n",
                argv[0], argv[0]);
        return 2;
    }
    printf("%d/%d correct\n", n - fails, n);
    sa_linux_close(&S);
    return fails ? 1 : 0;
}
