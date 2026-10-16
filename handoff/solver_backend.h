/*
 * Solver backend interface for the baselines.
 *
 * P1 uses a functional stand-in for SAT-Accel (decision 2026-10-16, option C):
 * the backend receives the SAT-Accel memory image the host built, reads the
 * clauses back out of it (cmd + clause_store pages, i.e. what the device's
 * copy-in sees) and solves them with MiniSat. A future backend (SAT-Accel on
 * ESP / FPGA) implements the same call on the same image.
 */
#ifndef RL_SOLVER_BACKEND_H
#define RL_SOLVER_BACKEND_H

#include <stdint.h>

#include "sataccel_image.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int sat;                        /* 1 SAT, 0 UNSAT, -1 error */
    uint64_t decisions, propagations, conflicts;
} rl_solve_result;

/* Solve a CNF given in CSR form. model[v-1] = +1 / -1 for SAT (may be NULL). */
rl_solve_result rl_minisat_solve(uint32_t num_vars, uint32_t num_clauses,
                                 const uint32_t *offsets, const int32_t *lits,
                                 int8_t *model);

/* Stand-in for SAT-Accel: decode the image, then rl_minisat_solve. */
rl_solve_result rl_solve_image(const sa_image *img, int8_t *model);

#ifdef __cplusplus
}
#endif
#endif
