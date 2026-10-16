/*
 * Readers for the workload files written by workload/dataset.py and
 * workload/reference_solve.py (formats documented there).
 */
#ifndef RL_TRACE_H
#define RL_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RL_CELLS 81
#define RL_DIGITS 9

/* data/sudoku/trace.bin: NVDLA output stand-in (int8 ip2 logits per cell). */
typedef struct {
    uint32_t num_puzzles, cells, classes, stride;
    float ip2_scale;
    uint8_t *data;          /* num_puzzles records of (cells + cells*stride) bytes */
} rl_trace;

int  rl_trace_read(rl_trace *t, const char *path);
void rl_trace_free(rl_trace *t);
const uint8_t *rl_trace_mask(const rl_trace *t, uint32_t p);     /* [cells]        */
const int8_t  *rl_trace_scores(const rl_trace *t, uint32_t p);   /* [cells][stride] */

/* data/sudoku/reference.bin: pysat reference results. */
typedef struct {
    uint32_t num_puzzles;
    uint8_t *data;          /* per puzzle: u8 sat, i8 solved[81], i8 predicted_givens[81] */
} rl_reference;

int  rl_reference_read(rl_reference *r, const char *path);
void rl_reference_free(rl_reference *r);
int  rl_reference_sat(const rl_reference *r, uint32_t p);
const int8_t *rl_reference_solved(const rl_reference *r, uint32_t p);
const int8_t *rl_reference_givens(const rl_reference *r, uint32_t p);

/*
 * Serial-baseline conversion step 1: argmax over digits 1..9 (ties -> lowest
 * digit, same as numpy) for each given cell; writes unit literals
 * var = 9*cell + digit (= 81r + 9c + d) to units[], returns how many.
 */
uint32_t rl_argmax_units(const rl_trace *t, uint32_t p, int32_t *units);

#ifdef __cplusplus
}
#endif
#endif
