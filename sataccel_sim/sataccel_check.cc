// Functional check of SAT-Accel (threaded simulation) on our images.
//
//   sataccel_check cnf <file.cnf> <expect: 1 SAT | 0 UNSAT>
//   sataccel_check sudoku <base.cnf> <trace.bin> <reference.bin> <out.csv> [N]
//
// Every SAT answer is checked against every clause of the instance. Sudoku
// mode also compares SAT/UNSAT and the solved grid with the PySAT reference;
// a different grid counts only if it is itself a valid solution (instances
// with a misread given can have several).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "cnf.h"
#include "sataccel_backend.h"
#include "trace.h"

static bool model_satisfies(const rl_cnf &c, const int8_t *model)
{
    for (uint32_t i = 0; i < c.num_clauses; i++) {
        bool sat = false;
        for (uint32_t k = c.offsets[i]; k < c.offsets[i + 1] && !sat; k++) {
            int32_t l = c.lits[k];
            sat = model[(l < 0 ? -l : l) - 1] == (l > 0 ? 1 : -1);
        }
        if (!sat) return false;
    }
    return true;
}

static int build(sa_image &img, const rl_cnf &c)
{
    sa_caps caps;
    sa_config cfg;
    rl_sataccel_caps(&caps);
    sa_default_config(&cfg);
    if (sa_image_init(&img, &caps, &cfg)) return -1;
    img.lean_clear = 1;
    return sa_build_image(&img, c.num_vars, c.num_clauses, c.offsets, c.lits);
}

static int cmd_cnf(const char *path, int expect)
{
    rl_cnf c;
    if (rl_cnf_read_dimacs(&c, path)) { std::fprintf(stderr, "cannot read %s\n", path); return 2; }
    sa_image img;
    if (build(img, c)) { std::printf("%s: image build failed (capacity)\n", path); return 1; }
    std::vector<int8_t> model(c.num_vars);
    rl_sataccel_result r;
    int st = rl_sataccel_solve_image(&img, model.data(), 300.0, &r);
    bool ok = st == expect && (st != 1 || model_satisfies(c, model.data()));
    std::printf("%-6s %-44s vars=%-6u clauses=%-7u result=%s expect=%s decisions=%u conflicts=%u sim=%.2fs\n",
                ok ? "PASS" : "FAIL", path, c.num_vars, c.num_clauses,
                st == 1 ? "SAT" : st == 0 ? "UNSAT" : st == -2 ? "TIMEOUT" : "ERROR",
                expect ? "SAT" : "UNSAT", r.decisions, r.backtracks, r.sim_seconds);
    return ok ? 0 : 1;
}

static bool valid_grid(const int8_t *g)
{
    for (int i = 0; i < 9; i++) {
        int row = 0, col = 0, box = 0;
        for (int j = 0; j < 9; j++) {
            row |= 1 << g[9 * i + j];
            col |= 1 << g[9 * j + i];
            box |= 1 << g[9 * (3 * (i / 3) + j / 3) + 3 * (i % 3) + j % 3];
        }
        if (row != 0x3fe || col != 0x3fe || box != 0x3fe) return false;
    }
    return true;
}

static int cmd_sudoku(const char *base_path, const char *trace_path, const char *ref_path, const char *csv,
                      uint32_t limit)
{
    rl_cnf base, inst;
    rl_trace tr;
    rl_reference ref;
    std::memset(&inst, 0, sizeof inst);
    if (rl_cnf_read_dimacs(&base, base_path) || rl_trace_read(&tr, trace_path) ||
        rl_reference_read(&ref, ref_path)) {
        std::fprintf(stderr, "cannot read inputs\n");
        return 2;
    }
    sa_image img;
    sa_caps caps;
    sa_config cfg;
    rl_sataccel_caps(&caps);
    sa_default_config(&cfg);
    if (sa_image_init(&img, &caps, &cfg)) return 2;
    img.lean_clear = 1;

    FILE *f = std::fopen(csv, "w");
    if (!f) return 2;
    std::fprintf(f, "puzzle,sat,ref_sat,model_ok,grid_match,iterations,decisions,retries,conflicts,restarts,sim_s\n");
    uint32_t n = limit && limit < tr.num_puzzles ? limit : tr.num_puzzles;
    uint32_t agree = 0, exact = 0, alt = 0, bad = 0, errors = 0;
    uint64_t dec = 0, confl = 0;
    double sim = 0;
    std::vector<int8_t> model(base.num_vars);
    int32_t units[RL_CELLS];
    for (uint32_t p = 0; p < n; p++) {
        uint32_t k = rl_argmax_units(&tr, p, units);
        if (rl_cnf_with_units(&inst, &base, units, k) ||
            sa_build_image(&img, inst.num_vars, inst.num_clauses, inst.offsets, inst.lits)) {
            errors++;
            continue;
        }
        rl_sataccel_result r;
        int st = rl_sataccel_solve_image(&img, model.data(), 300.0, &r);
        if (st == -2) {
            std::fprintf(stderr, "puzzle %u: timeout; aborting run\n", p);
            std::fclose(f);
            return 1;
        }
        int ref_sat = rl_reference_sat(&ref, p);
        bool model_ok = st != 1 || model_satisfies(inst, model.data());
        int8_t grid[RL_CELLS] = {0};
        if (st == 1)
            for (uint32_t v = 0; v < 729; v++)
                if (model[v] > 0) grid[v / 9] = (int8_t)(v % 9 + 1);
        bool same = !std::memcmp(grid, rl_reference_solved(&ref, p), RL_CELLS);
        if (st < 0) errors++;
        if (st == ref_sat) agree++;
        if (st == ref_sat && model_ok && same) exact++;
        else if (st == ref_sat && st == 1 && model_ok && valid_grid(grid)) alt++;
        else bad++;
        dec += r.decisions;
        confl += r.backtracks;
        sim += r.sim_seconds;
        std::fprintf(f, "%u,%d,%d,%d,%d,%u,%u,%u,%u,%u,%.6f\n", p, st, ref_sat, model_ok, same, r.iterations,
                     r.decisions, r.retries, r.backtracks, r.restarts, r.sim_seconds);
        if ((p + 1) % 100 == 0) std::fprintf(stderr, "%u/%u puzzles\n", p + 1, n);
    }
    std::fclose(f);
    std::printf("SAT-Accel functional simulation, Sudoku\n"
                "puzzles                         %u\n"
                "errors                          %u\n"
                "SAT/UNSAT == PySAT reference    %u/%u\n"
                "grid == reference               %u\n"
                "valid alternative solution      %u\n"
                "wrong                           %u\n"
                "mean decisions / conflicts      %.3f / %.3f\n"
                "mean simulation time per puzzle %.3f s\n",
                n, errors, agree, n, exact, alt, bad, (double)dec / n, (double)confl / n, sim / n);
    return (errors == 0 && bad == 0) ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 4 && !std::strcmp(argv[1], "cnf")) return cmd_cnf(argv[2], std::atoi(argv[3]));
    if (argc >= 6 && !std::strcmp(argv[1], "sudoku"))
        return cmd_sudoku(argv[2], argv[3], argv[4], argv[5], argc > 6 ? (uint32_t)std::atoi(argv[6]) : 0);
    std::fprintf(stderr, "usage:\n  %s cnf <file.cnf> <1|0>\n  %s sudoku <base.cnf> <trace.bin> <reference.bin> <out.csv> [N]\n",
                 argv[0], argv[0]);
    return 2;
}
