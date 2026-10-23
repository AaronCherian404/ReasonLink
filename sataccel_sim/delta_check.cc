// Delta-image check for ReasonLink (mechanism iii, DRAM-resident variant).
//
// SAT-Accel's host conversion turns every unit clause into a level-0 fixed
// decision (answerStack prefix + miscCounters[3]) and also keeps it as a
// clause. If the givens are passed only as fixed decisions, a puzzle's image
// is the base formula's image plus that prefix and count. This runs all
// puzzles that way through SAT-Accel's functional simulation, with ONE base
// image restored between puzzles (answerStack and miscCounters, which the
// kernels overwrite), and compares SAT/UNSAT and grids with the PySAT
// reference.
//
//   delta_check <data/sudoku> [N]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "cnf.h"
#include "sataccel_backend.h"
#include "trace.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <data/sudoku> [N]\n", argv[0]);
        return 2;
    }
    char p[4096];
    rl_cnf base;
    rl_trace tr;
    rl_reference ref;
    std::snprintf(p, sizeof p, "%s/base.cnf", argv[1]);
    if (rl_cnf_read_dimacs(&base, p)) return 2;
    std::snprintf(p, sizeof p, "%s/trace.bin", argv[1]);
    if (rl_trace_read(&tr, p)) return 2;
    std::snprintf(p, sizeof p, "%s/reference.bin", argv[1]);
    if (rl_reference_read(&ref, p)) return 2;
    uint32_t n = argc > 2 ? (uint32_t)std::atoi(argv[2]) : tr.num_puzzles;

    sa_caps caps;
    sa_config cfg;
    rl_sataccel_caps(&caps);
    sa_default_config(&cfg);
    sa_image img;
    if (sa_image_init(&img, &caps, &cfg) ||
        sa_build_image(&img, base.num_vars, base.num_clauses, base.offsets, base.lits))
        return 2;
    if (img.decision_count != 0) {
        std::fprintf(stderr, "base formula has %u decisions; expected 0\n", img.decision_count);
        return 2;
    }
    const uint32_t nv = img.num_vars;
    std::vector<int32_t> base_answer(img.answer_stack, img.answer_stack + nv);
    int32_t base_misc[256];
    std::memcpy(base_misc, img.misc, sizeof base_misc);

    uint32_t agree = 0, exact = 0, other = 0, errors = 0;
    std::vector<int8_t> model(nv);
    for (uint32_t k = 0; k < n; k++) {
        int32_t units[RL_CELLS];
        uint32_t u = rl_argmax_units(&tr, k, units);
        // Restore what the kernels overwrite, then write the delta: the givens
        // as fixed decisions in std::set order (sorted, unique)
        std::memcpy(img.answer_stack, base_answer.data(), nv * sizeof(int32_t));
        std::memcpy(img.misc, base_misc, sizeof base_misc);
        std::vector<int32_t> dec(units, units + u);
        std::sort(dec.begin(), dec.end());
        dec.erase(std::unique(dec.begin(), dec.end()), dec.end());
        std::memcpy(img.answer_stack, dec.data(), dec.size() * sizeof(int32_t));
        img.misc[3] = (int32_t)dec.size();

        rl_sataccel_result r;
        int st = rl_sataccel_solve_image(&img, model.data(), 300.0, &r);
        if (st < 0) {
            errors++;
            continue;
        }
        int ref_sat = rl_reference_sat(&ref, k);
        int8_t grid[RL_CELLS] = {0};
        if (st == 1)
            for (uint32_t v = 0; v < 729; v++)
                if (model[v] > 0) grid[v / 9] = (int8_t)(v % 9 + 1);
        if (st == ref_sat) agree++;
        if (st == ref_sat && (st == 0 || !std::memcmp(grid, rl_reference_solved(&ref, k), RL_CELLS))) exact++;
        else {
            other++;
            std::printf("different grid: puzzle %u\n", k);
        }
        if ((k + 1) % 100 == 0) std::fprintf(stderr, "%u/%u\n", k + 1, n);
    }
    std::printf("givens as fixed decisions on one DRAM-resident base image (restored per puzzle)\n"
                "puzzles %u, errors %u, SAT/UNSAT == reference %u/%u, identical grid or UNSAT %u, other %u\n",
                n, errors, agree, n, exact, other);
    return (errors == 0 && agree == n) ? 0 : 1;
}
