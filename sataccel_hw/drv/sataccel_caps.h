/*
 * Capacities of the SAT-Accel hardware we build (patches/0001-capacity-vcu118
 * applied to fpga_solver.h). Images for the device must be built with these:
 * miscCounters[5] tells the solver its literal-store size. `make check-caps`
 * in sataccel_hw/ verifies they match the patched header.
 */
#ifndef SATACCEL_CAPS_H
#define SATACCEL_CAPS_H

#include "sataccel_image.h"

#define SA_HW_MAX_VARS              32768u     /* _FPGA_MAX_LITERALS (unchanged) */
/* Instances must have num_vars <= SA_HW_MAX_VARS / 2: pqHandler's swapLower
 * reads heap children before bounds-checking them (see the patch). */
#define SA_HW_VAR_LIMIT             (SA_HW_MAX_VARS / 2)
#define SA_HW_MAX_CLAUSES           65536u     /* _FPGA_MAX_CLAUSES */
#define SA_HW_MAX_LITERAL_ELEMENTS  524288u    /* _HOST_MAX_LITERAL_ELEMENTS */
#define SA_HW_MAX_CLAUSE_ELEMENTS   524288u    /* _HOST_MAX_CLAUSE_ELEMENTS */

/* Device-memory base for the testbench and the bare-metal test. On Linux the
 * kernel driver allocates from ESP's reserved third-party pool instead (in
 * the reasonlink-vcu118 SoC: 0xA0200000-0xBFFFFFFF, which contains this). */
#define SA_HW_MEM_BASE              0xB0000000u

static inline void sa_hw_caps(sa_caps *c)
{
    c->max_vars = SA_HW_MAX_VARS;
    c->max_clauses = SA_HW_MAX_CLAUSES;
    c->max_literal_elements = SA_HW_MAX_LITERAL_ELEMENTS;
    c->max_clause_elements = SA_HW_MAX_CLAUSE_ELEMENTS;
}

#endif
