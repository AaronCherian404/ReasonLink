// MiniSat (third_party/minisat @ 37dc6c6) behind the C backend interface.
#include <cstdlib>

#include "minisat/core/Solver.h"
#include "solver_backend.h"

extern "C" rl_solve_result rl_minisat_solve(uint32_t num_vars, uint32_t num_clauses,
                                            const uint32_t *offsets, const int32_t *lits,
                                            int8_t *model)
{
    rl_solve_result r = {-1, 0, 0, 0};
    Minisat::Solver s;
    s.verbosity = 0;
    while ((uint32_t)s.nVars() < num_vars) s.newVar();

    Minisat::vec<Minisat::Lit> cl;
    bool ok = true;
    for (uint32_t i = 0; i < num_clauses && ok; i++) {
        cl.clear();
        for (uint32_t k = offsets[i]; k < offsets[i + 1]; k++) {
            int32_t l = lits[k];
            int32_t v = (l < 0 ? -l : l) - 1;
            if (v < 0 || (uint32_t)v >= num_vars) return r;
            cl.push(Minisat::mkLit(v, l < 0));
        }
        ok = s.addClause_(cl);      // false: formula already UNSAT at level 0
    }
    r.sat = ok && s.solve() ? 1 : 0;
    r.decisions = s.decisions;
    r.propagations = s.propagations;
    r.conflicts = s.conflicts;
    if (r.sat && model)
        for (uint32_t v = 0; v < num_vars; v++)
            model[v] = s.modelValue(v) == Minisat::l_True ? 1 : -1;
    return r;
}

extern "C" rl_solve_result rl_solve_image(const sa_image *img, int8_t *model)
{
    uint32_t *off;
    int32_t *lits;
    long nc = sa_decode_clauses(img, &off, &lits);
    if (nc < 0) {
        rl_solve_result bad = {-1, 0, 0, 0};
        return bad;
    }
    rl_solve_result r = rl_minisat_solve(img->num_vars, (uint32_t)nc, off, lits, model);
    std::free(off);
    std::free(lits);
    return r;
}
