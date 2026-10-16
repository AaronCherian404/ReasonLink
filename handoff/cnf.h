/* Minimal CNF container (CSR) and DIMACS reader. */
#ifndef RL_CNF_H
#define RL_CNF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t num_vars, num_clauses;
    uint32_t *offsets;      /* num_clauses + 1 */
    int32_t  *lits;         /* offsets[num_clauses] */
    uint32_t cap_clauses, cap_lits;
} rl_cnf;

/* Read DIMACS (comment lines 'c', header 'p cnf V C', clauses ending in 0,
 * stops at a '%' line). Returns 0 on success. */
int  rl_cnf_read_dimacs(rl_cnf *cnf, const char *path);
void rl_cnf_free(rl_cnf *cnf);

/* Copy `base` into `dst` and append one unit clause per literal in units[]. */
int  rl_cnf_with_units(rl_cnf *dst, const rl_cnf *base, const int32_t *units, uint32_t n_units);

#ifdef __cplusplus
}
#endif
#endif
