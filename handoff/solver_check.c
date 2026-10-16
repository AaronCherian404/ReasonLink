/*
 * Stage 2 check: "the SAT solver solves Sudoku CNFs in simulation" with the
 * P1 stand-in (option C). For every puzzle:
 *   trace (int8 logits) -> argmax units -> SAT-Accel image (C port)
 *   -> device-side decode -> MiniSat -> grid
 * and compare against the pysat reference (SAT/UNSAT, solved grid, givens).
 *
 * Usage: solver_check <base.cnf> <trace.bin> <reference.bin> [report.txt]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cnf.h"
#include "sataccel_image.h"
#include "solver_backend.h"
#include "trace.h"

/* Does the assignment "cell c holds grid[c]" satisfy every clause of cnf? */
static int grid_satisfies(const rl_cnf *cnf, const int8_t *grid)
{
    for (uint32_t i = 0; i < cnf->num_clauses; i++) {
        int sat = 0;
        for (uint32_t k = cnf->offsets[i]; k < cnf->offsets[i + 1] && !sat; k++) {
            int32_t l = cnf->lits[k], v = (l < 0 ? -l : l) - 1;
            int val = grid[v / 9] == v % 9 + 1;
            sat = l > 0 ? val : !val;
        }
        if (!sat) return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s base.cnf trace.bin reference.bin [report.txt]\n", argv[0]);
        return 2;
    }
    rl_cnf base, inst;
    rl_trace tr;
    rl_reference ref;
    memset(&inst, 0, sizeof inst);
    if (rl_cnf_read_dimacs(&base, argv[1]) || rl_trace_read(&tr, argv[2]) ||
        rl_reference_read(&ref, argv[3]) || ref.num_puzzles != tr.num_puzzles) {
        fprintf(stderr, "cannot read inputs\n");
        return 1;
    }
    sa_caps caps;
    sa_config cfg;
    sa_default_caps(&caps);
    sa_default_config(&cfg);
    sa_image img;
    if (sa_image_init(&img, &caps, &cfg)) return 1;
    img.lean_clear = 1;   /* image bytes are identical either way; faster here */

    int32_t units[RL_CELLS];
    int8_t model[729];
    uint32_t n = tr.num_puzzles, sat = 0, givens_ok = 0, sat_ok = 0, grid_ok = 0, errors = 0;
    uint32_t alt_ok = 0, alt_bad = 0;   /* grid mismatches: both valid / not */
    uint64_t dec = 0, prop = 0, confl = 0;
    for (uint32_t p = 0; p < n; p++) {
        uint32_t k = rl_argmax_units(&tr, p, units);
        int8_t grid[RL_CELLS] = {0};
        for (uint32_t i = 0; i < k; i++) grid[(units[i] - 1) / 9] = (int8_t)((units[i] - 1) % 9 + 1);
        givens_ok += !memcmp(grid, rl_reference_givens(&ref, p), RL_CELLS);

        if (rl_cnf_with_units(&inst, &base, units, k) ||
            sa_build_image(&img, inst.num_vars, inst.num_clauses, inst.offsets, inst.lits)) {
            errors++;
            continue;
        }
        rl_solve_result r = rl_solve_image(&img, model);
        if (r.sat < 0) { errors++; continue; }
        int8_t solved[RL_CELLS] = {0};
        if (r.sat)
            for (uint32_t v = 0; v < 729; v++)
                if (model[v] > 0) solved[v / 9] = (int8_t)(v % 9 + 1);
        sat += r.sat;
        sat_ok += r.sat == rl_reference_sat(&ref, p);
        if (!memcmp(solved, rl_reference_solved(&ref, p), RL_CELLS)) {
            grid_ok++;
        } else if (r.sat && grid_satisfies(&inst, solved) &&
                   grid_satisfies(&inst, rl_reference_solved(&ref, p))) {
            alt_ok++;           /* instance has several solutions; both valid */
            fprintf(stderr, "multi-solution puzzle %u\n", p);
        } else {
            alt_bad++;
        }
        dec += r.decisions;
        prop += r.propagations;
        confl += r.conflicts;
    }

    char report[2048];
    snprintf(report, sizeof report,
             "Stage 2 solver check (stand-in: SAT-Accel image -> decode -> MiniSat)\n"
             "puzzles                         %u\n"
             "errors (build/decode)           %u\n"
             "predicted givens == reference   %u/%u\n"
             "SAT/UNSAT == reference          %u/%u\n"
             "solved grid == reference        %u/%u\n"
             "  differing, both grids valid   %u (multi-solution instances)\n"
             "  differing, invalid            %u\n"
             "SAT                             %u (UNSAT %u)\n"
             "mean decisions/propagations/conflicts  %.3f / %.1f / %.3f\n",
             n, errors, givens_ok, n, sat_ok, n, grid_ok, n, alt_ok, alt_bad, sat, n - sat,
             (double)dec / n, (double)prop / n, (double)confl / n);
    fputs(report, stdout);
    if (argc > 4) {
        FILE *f = fopen(argv[4], "w");
        if (f) { fputs(report, f); fclose(f); }
    }
    sa_image_free(&img);
    rl_cnf_free(&base);
    rl_cnf_free(&inst);
    rl_trace_free(&tr);
    rl_reference_free(&ref);
    return (errors == 0 && sat_ok == n && alt_bad == 0 && givens_ok == n) ? 0 : 1;
}
