/*
 * SAT-Accel as a solver backend, in functional simulation.
 *
 * Runs SAT-Accel's seven kernels (solver, clause_store_handler,
 * location_handler, restartCalculator, timer, pqHandler, message), compiled
 * from the pinned, unmodified sources, as concurrent threads connected by
 * streams exactly as in its k2k.cfg. Like Vitis sw_emu, this executes the
 * kernels' software path (FPGA_HW undefined). Results are functional
 * (SAT/UNSAT, assignment, SAT-Accel's own search counters); wall time is
 * simulation time, not hardware cycles.
 */
#ifndef RL_SATACCEL_BACKEND_H
#define RL_SATACCEL_BACKEND_H

#include <stdint.h>

#include "sataccel_image.h"
#include "solver_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int status;              /* 1 SAT, 0 UNSAT, -1 kernel error, -2 timeout */
    uint32_t iterations;     /* miscCounters[0]: solve-loop iterations */
    uint32_t decisions;      /* miscCounters[1] */
    uint32_t retries;        /* miscCounters[2]: flipped-literal retries */
    uint32_t backtracks;     /* miscCounters[3]: conflicts */
    uint32_t restarts;       /* miscCounters[4] */
    double sim_seconds;      /* wall time of the threaded simulation */
} rl_sataccel_result;

/*
 * Solve the image. model[v] = +1/-1 for v in [0, num_vars) when SAT (may be
 * NULL). timeout_s bounds the whole run; on timeout the process cannot
 * reclaim the kernel threads, so callers should treat -2 as fatal.
 */
int rl_sataccel_solve_image(const sa_image *img, int8_t *model, double timeout_s,
                            rl_sataccel_result *out);

/* Same, in the generic backend form used by the baselines. */
rl_solve_result rl_sataccel_solve(const sa_image *img, int8_t *model);

#ifdef __cplusplus
}
#endif
#endif
