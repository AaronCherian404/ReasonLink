/*
 * How much of SAT-Accel's memory image changes from puzzle to puzzle?
 * Builds the image of the base Sudoku formula (no givens) and of each
 * puzzle (base + predicted givens as unit clauses), and counts, per region,
 * the bytes of the puzzle image's used range that differ from the base
 * image. Sizes the delta-transfer mechanism (ReasonLink (iii)).
 *
 * Usage: image_delta <data/sudoku> [N]   (VCU118 capacities)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cnf.h"
#include "sataccel_image.h"
#include "trace.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <data/sudoku> [N]\n", argv[0]);
        return 2;
    }
    char p[4096];
    rl_cnf base, inst;
    rl_trace tr;
    memset(&inst, 0, sizeof inst);
    snprintf(p, sizeof p, "%s/base.cnf", argv[1]);
    if (rl_cnf_read_dimacs(&base, p)) return 2;
    snprintf(p, sizeof p, "%s/trace.bin", argv[1]);
    if (rl_trace_read(&tr, p)) return 2;
    uint32_t n = argc > 2 ? (uint32_t)atoi(argv[2]) : tr.num_puzzles;

    sa_caps caps = {32768, 65536, 524288, 524288};   /* VCU118 build (sataccel_caps.h) */
    sa_config cfg;
    sa_default_config(&cfg);
    sa_image b, x;
    if (sa_image_init(&b, &caps, &cfg) || sa_image_init(&x, &caps, &cfg)) return 2;
    if (sa_build_image(&b, base.num_vars, base.num_clauses, base.offsets, base.lits)) return 2;
    sa_region rb[SA_NUM_REGIONS], rx[SA_NUM_REGIONS];
    sa_regions(&b, rb);

    double used_sum[SA_NUM_REGIONS] = {0}, diff_sum[SA_NUM_REGIONS] = {0};
    for (uint32_t k = 0; k < n; k++) {
        int32_t units[RL_CELLS];
        uint32_t u = rl_argmax_units(&tr, k, units);
        if (rl_cnf_with_units(&inst, &base, units, u) ||
            sa_build_image(&x, inst.num_vars, inst.num_clauses, inst.offsets, inst.lits)) return 2;
        sa_regions(&x, rx);
        for (int r = 0; r < SA_NUM_REGIONS; r++) {
            const uint8_t *pb = sa_region_ptr(&b, r), *px = sa_region_ptr(&x, r);
            size_t nb = rb[r].used, nx = rx[r].used, d = 0;
            for (size_t i = 0; i < nx; i++) d += i >= nb || pb[i] != px[i];
            used_sum[r] += nx;
            diff_sum[r] += d;
        }
    }
    printf("base formula: %u vars, %u clauses; %u puzzles (VCU118 capacities)\n", base.num_vars,
           base.num_clauses, n);
    printf("%-14s %14s %16s %10s\n", "region", "used B/puzzle", "differs B/puzzle", "differs %");
    double tu = 0, td = 0;
    for (int r = 0; r < SA_NUM_REGIONS; r++) {
        printf("%-14s %14.0f %16.0f %9.2f%%\n", rx[r].name, used_sum[r] / n, diff_sum[r] / n,
               used_sum[r] ? 100 * diff_sum[r] / used_sum[r] : 0.0);
        tu += used_sum[r];
        td += diff_sum[r];
    }
    printf("%-14s %14.0f %16.0f %9.2f%%\n", "total", tu / n, td / n, 100 * td / tu);
    return 0;
}
