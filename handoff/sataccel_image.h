/*
 * Standalone C port of SAT-Accel's host-side memory-image construction
 * (third_party/FPGA25_SAT_Accel/src/host.cpp, parseDIMACS, lines 71-376).
 *
 * This is the "representation conversion" step of the serial baseline: the
 * host turns a CNF (base formula + predicted givens as unit clauses) into the
 * buffers SAT-Accel's kernels copy in (copy_in.cpp, clause_store_handler.cpp).
 * Layout and field order follow the original exactly; the golden test in
 * handoff/golden_test/ checks byte equality against the original code.
 *
 * Regions (all little-endian; "allocated" is what host.cpp migrates per run):
 *   clause_store  int32[max_clause_elements]   clauses in 4-word pages; word 3
 *                                              of a page = index of next page
 *                                              word, or 0 at the clause end
 *   cmd           {u32 addressStart, u32 numElements}[max_clauses]
 *   cls_states    {i32 xor of literals, i32 remainingUnassigned}[max_clauses]
 *   lit_store     int32[max_literal_elements]  per-variable, per-polarity
 *                                              occurrence lists (clause ids)
 *                                              in lit_page_size-word pages
 *   lmd           64 bytes (ap_uint<512>) per variable, fields per
 *                                              data_structures.h LMD_* macros
 *   answer_stack  int32[num_vars]              sorted decision set (unit
 *                                              literals + unused variables)
 *   misc          int32[256]                   miscCounters
 *   track_lbd     uint32[2 * 10]               migrated but never initialised
 *                                              by host.cpp; we send zeros
 *
 * SAT-Accel calls variables "literals"; here num_vars = DIMACS variables.
 */
#ifndef SATACCEL_IMAGE_H
#define SATACCEL_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capacity parameters (fpga_solver.h). Defaults match the U55C build. */
typedef struct {
    uint32_t max_vars;              /* _FPGA_MAX_LITERALS          = 32768   */
    uint32_t max_clauses;           /* _FPGA_MAX_CLAUSES           = 131072  */
    uint32_t max_literal_elements;  /* _HOST_MAX_LITERAL_ELEMENTS  = 1048576 */
    uint32_t max_clause_elements;   /* _HOST_MAX_CLAUSE_ELEMENTS   = 1048576 */
} sa_caps;

/* configuration.json */
typedef struct {
    uint32_t lit_page_size;         /* _HOST_LITERAL_PAGE_SIZE   16    */
    uint32_t reset_multiplier;      /* _HOST_RESET_MULTIPLIER    100   */
    int      positive_phase;        /* _HOST_POSITIVE_LIT_PHASE_VAL false */
    double   decay_factor;          /* _HOST_DECAY_FACTOR        0.95  */
} sa_config;

#define SA_LMD_BYTES 64
#define SA_TRACK_LBD_WORDS 20
#define SA_CLAUSE_PAGE 4            /* hard-coded "4-1" in host.cpp */

typedef struct {
    sa_caps   caps;
    sa_config cfg;

    int32_t  *clause_store;         /* caps.max_clause_elements        */
    uint32_t *cmd;                  /* 2 * caps.max_clauses            */
    int32_t  *cls_states;           /* 2 * caps.max_clauses            */
    int32_t  *lit_store;            /* caps.max_literal_elements       */
    uint8_t  *lmd;                  /* SA_LMD_BYTES * num_vars         */
    int32_t  *answer_stack;         /* num_vars                        */
    int32_t   misc[256];
    uint32_t  track_lbd[SA_TRACK_LBD_WORDS];

    uint32_t num_vars, num_clauses;
    uint32_t clause_elements, literal_elements;
    uint32_t decision_count;        /* miscCounters[3] */
    uint32_t presolved_count;

    /*
     * 0 (default, faithful): every build clears the whole allocated buffers,
     *   as host.cpp does when it allocates and memsets them per instance.
     * 1 (lean): clear only the ranges the previous build wrote. Same image
     *   bytes; models a host that keeps buffers across instances.
     */
    int lean_clear;

    /* scratch reused across builds (owned by the image) */
    int32_t  *scratch;
    size_t    scratch_cap;
} sa_image;

/* One row per region: bytes host.cpp migrates vs. bytes that carry data. */
typedef struct {
    const char *name;
    size_t allocated;
    size_t used;
} sa_region;

#define SA_NUM_REGIONS 8

void sa_default_caps(sa_caps *caps);
void sa_default_config(sa_config *cfg);

/* Allocate buffers for up to caps->max_vars variables. Returns 0 on success. */
int  sa_image_init(sa_image *img, const sa_caps *caps, const sa_config *cfg);
void sa_image_free(sa_image *img);

/*
 * Build the image for a CNF given in CSR form: clause i has literals
 * lits[offsets[i] .. offsets[i+1]). Clauses are de-duplicated and sorted by
 * |literal| exactly as host.cpp does (input arrays are not modified).
 * Returns 0 on success, or a negative code if a capacity limit is exceeded
 * (host.cpp exits in that case).
 */
int sa_build_image(sa_image *img, uint32_t num_vars, uint32_t num_clauses,
                   const uint32_t *offsets, const int32_t *lits);

/* Fill rows[SA_NUM_REGIONS]; returns total allocated bytes. */
size_t sa_regions(const sa_image *img, sa_region rows[SA_NUM_REGIONS]);

/* Raw region pointers in the same order as sa_regions (for copies/dumps). */
const void *sa_region_ptr(const sa_image *img, int region);

/*
 * Device-side view used by the stand-in solver: walk cmd + clause_store pages
 * and return the clauses (CSR, caller frees *out_offsets / *out_lits).
 * Returns the number of clauses, or -1 on a malformed image.
 */
long sa_decode_clauses(const sa_image *img, uint32_t **out_offsets, int32_t **out_lits);

/* Read an lmd bit field [lo, hi] (inclusive) of variable v (0-based). */
uint64_t sa_lmd_field(const sa_image *img, uint32_t v, unsigned lo, unsigned hi);

#ifdef __cplusplus
}
#endif
#endif
