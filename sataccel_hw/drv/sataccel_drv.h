/*
 * SAT-Accel device driver core: what XRT does for SAT-Accel's host.cpp,
 * without XRT. Shared by the RTL testbench (record backend), the bare-metal
 * test and the Linux user-space library (MMIO backends).
 *
 * Device: the SAT-Accel subsystem (sataccel_hw/rtl/sataccel_top.v) behind a
 * 1 MB register window. Kernel control ports sit at SA_KERNEL_BASE_*
 * (64 KB each, Vivado block-design address map); our shell registers
 * (counters) at SA_SHELL_BASE. Memory: one physically contiguous region the
 * caller provides; the driver places SAT-Accel's buffers in it.
 */
#ifndef SATACCEL_DRV_H
#define SATACCEL_DRV_H

#include <stddef.h>
#include <stdint.h>

#include "sataccel_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register map (byte offsets in the device window) */
#define SA_KERNEL_BASE_MESSAGE      0x00000u
#define SA_KERNEL_BASE_CLAUSE_STORE 0x10000u
#define SA_KERNEL_BASE_LOCATION     0x20000u
#define SA_KERNEL_BASE_TIMER        0x30000u
#define SA_KERNEL_BASE_RESTART      0x40000u
#define SA_KERNEL_BASE_PQ           0x50000u
#define SA_KERNEL_BASE_SOLVER       0x60000u
#define SA_SHELL_BASE               0xF0000u

/* Shell registers (sataccel_shell.v) */
#define SA_SHELL_ID          0x00   /* reads SA_SHELL_ID_VALUE */
#define SA_SHELL_CTRL        0x04   /* bit 0: clear counters (self-clearing) */
#define SA_SHELL_STATUS      0x08   /* bit 0: busy, bit 1: solver irq */
#define SA_SHELL_CYCLES      0x10   /* 64-bit counters: lo at +0, hi at +4;  */
#define SA_SHELL_BUSY        0x18   /* reading lo latches hi                  */
#define SA_SHELL_MEM_ACTIVE  0x20   /* cycles with any AXI beat               */
#define SA_SHELL_MEM_STALL   0x28   /* cycles with a request valid, not ready */
#define SA_SHELL_RD_BEATS    0x30
#define SA_SHELL_WR_BEATS    0x38
#define SA_SHELL_ID_VALUE    0x5A7A0001u

/* ap_ctrl_chain control register bits (HLS) */
#define SA_AP_START 0x1u
#define SA_AP_DONE  0x2u
#define SA_AP_IDLE  0x4u

/* Register access: the backend */
typedef struct {
    void (*wr32)(void *ctx, uint32_t off, uint32_t val);
    uint32_t (*rd32)(void *ctx, uint32_t off);
    /* Wait until (rd32(off) & mask) == val; return 0, or -1 on timeout.
     * NULL: the driver polls with rd32 (spin). */
    int (*poll)(void *ctx, uint32_t off, uint32_t mask, uint32_t val);
    /* Notify that [addr, addr+len) of device memory was written by the CPU
     * (record backend: emit the contents; cache-incoherent targets: flush).
     * May be NULL. */
    void (*mem_written)(void *ctx, uint64_t addr, const void *src, size_t len);
    /* Ask for [addr, addr+len) to be read back after the run (record
     * backend: dump request; others: invalidate). May be NULL. */
    void (*mem_read_back)(void *ctx, uint64_t addr, void *dst, size_t len);
    void *ctx;
} sa_hw_ops;

/* Device buffers, in host.cpp's order and sizes (allocated at capacity) */
enum {
    SA_BUF_CLAUSE_STORE, SA_BUF_CMD, SA_BUF_USED_CLS_ID_BUCKETS, SA_BUF_TRACK_LBD,
    SA_BUF_CLS_STATES, SA_BUF_LIT_STORE, SA_BUF_ANSWER_STACK, SA_BUF_LMD,
    SA_BUF_MISC, SA_BUF_DEBUG, SA_NUM_BUFS
};

typedef struct {
    sa_hw_ops hw;
    uint64_t mem_phys;          /* device address of the region */
    uint8_t *mem_virt;          /* CPU address of the same region (may be NULL: record-only) */
    size_t mem_size;
    uint64_t buf_off[SA_NUM_BUFS];
    size_t buf_size[SA_NUM_BUFS];
    uint32_t max_vars;          /* caps.max_vars */
    /* last run */
    uint32_t num_vars, num_clauses;
} sa_dev;

/* Bytes of device memory the buffers need for the given capacities. */
size_t sa_dev_mem_required(const sa_caps *caps);

/* Lay the buffers out in [mem_phys, mem_phys+mem_size). 0 on success. */
int sa_dev_init(sa_dev *d, const sa_hw_ops *hw, uint64_t mem_phys, void *mem_virt,
                size_t mem_size, const sa_caps *caps);

/* Read the shell ID register; 0 if it matches. */
int sa_dev_probe(sa_dev *d);

/* T_xfer: copy the image regions into the device buffers. used_only: copy only
 * the used prefix of each region (lean); otherwise the allocated sizes, as
 * host.cpp migrates them. Returns bytes copied, or (size_t)-1 if the instance
 * has more than max_vars / 2 variables (pqHandler's heap reads children at
 * 2*pos+2 before bounds-checking them; see patches/0001). */
size_t sa_dev_load(sa_dev *d, const sa_image *img, int used_only);

/* Delta load (ReasonLink mechanism iii): the device buffers already hold the
 * image of a base formula with no decisions (sa_dev_load once); pass an
 * instance's unit literals only as level-0 fixed decisions. Writes the
 * answerStack (zeroed, then the sorted unique literals as its prefix) and
 * miscCounters (the base image's, with [3] = their count), the two buffers
 * the kernels overwrite. lits must be sorted ascending and unique (Sudoku
 * givens in cell order are). Returns bytes written, or (size_t)-1 if base is
 * unsuitable (it has decisions of its own). */
size_t sa_dev_load_decisions(sa_dev *d, const sa_image *base, const int32_t *lits, uint32_t k);

/* ReasonLink phase B (rtl/reasonlink_core.v, block design built with
 * REASONLINK=1): the tile turns NVDLA's logits into fixed-decision literals
 * and writes the delta itself (what sa_dev_load_decisions does on the CPU).
 *   sa_rl_setup   once: the base image (sa_dev_load'ed, no decisions) and
 *                 NVDLA's output tensor layout (channel group 8 or 32, surface
 *                 stride for group 8); 0 on success, -1 if no ReasonLink
 *   sa_rl_begin   per puzzle, before the first cell
 *   sa_rl_push    per given cell, in increasing cell order, as soon as NVDLA
 *                 has written its logits at logit_addr (channel 0, 8-byte aligned)
 *   sa_rl_commit  after the last cell: wait until the delta is written;
 *                 0 on success, -1 on error/timeout. Then sa_dev_start as usual. */
#define SA_RL_BASE 0x70000u
#define SA_RL_ID_VALUE 0x524C0001u
int sa_rl_setup(sa_dev *d, const sa_image *base, uint32_t group, uint32_t surf_stride);
void sa_rl_begin(sa_dev *d);
void sa_rl_push(sa_dev *d, uint32_t logit_addr, uint32_t cell);
int sa_rl_commit(sa_dev *d);

/* Program kernel arguments (host.cpp setArg) and start the seven kernels in
 * host.cpp's enqueue order. */
void sa_dev_start(sa_dev *d, const sa_image *img);

/* Wait for the solver to finish, then for every kernel to return to idle.
 * 0 on success, -1 on timeout. */
int sa_dev_wait(sa_dev *d);

/* Results (after sa_dev_wait): 1 SAT, 0 UNSAT, -1 error (miscCounters[6]).
 * model[v] = +1/-1 for v < num_vars when SAT (may be NULL). */
typedef struct {
    int status;
    uint32_t iterations, decisions, retries, backtracks, restarts;
} sa_dev_result;
int sa_dev_result_read(sa_dev *d, int8_t *model, sa_dev_result *r);

/* 64-bit shell counter at offset off (SA_SHELL_CYCLES, ...). */
uint64_t sa_dev_counter(sa_dev *d, uint32_t off);

#ifdef __cplusplus
}
#endif
#endif
