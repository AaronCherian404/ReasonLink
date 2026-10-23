/*
 * Host test double for libsataccel's Linux backend (sataccel_linux.c): the
 * same API over a fake device, so rl_board's pipeline (double buffering,
 * result decoding, checks) can be tested off the board.
 *
 * The fake device is a 1 MB register file plus "DMA memory" in host RAM.
 * Writing ap_start to the solver runs SAT-Accel's functional simulation
 * (sataccel_sim, P2-1) on the image found at the addresses the driver
 * programmed, writes miscCounters and answerStack back to that memory as the
 * kernels do, and sets ap_done/ap_idle on every kernel.
 */
#include <stdlib.h>
#include <string.h>

#include "sataccel_backend.h"
#include "sataccel_linux.h"
#include "xclause_store_handler_hw.h"
#include "xpqhandler_hw.h"
#include "xsolver_hw.h"

#define FAKE_PHYS 0xB0000000ull
#define FAKE_MEM  (64ull << 20)

static uint8_t *mem_at(sa_linux *s, uint64_t addr) { return s->mem + (addr - s->mem_phys); }

static uint64_t reg64(sa_linux *s, uint32_t off)
{
    return (uint64_t)s->regs[off / 4] | ((uint64_t)s->regs[off / 4 + 1] << 32);
}

static void run_solver(sa_linux *s)
{
    const uint32_t C = SA_KERNEL_BASE_CLAUSE_STORE, P = SA_KERNEL_BASE_PQ, S = SA_KERNEL_BASE_SOLVER;
    sa_image img;
    memset(&img, 0, sizeof img);
    rl_sataccel_caps(&img.caps);
    sa_default_config(&img.cfg);
    img.num_vars = s->regs[(P + XPQHANDLER_CONTROL_ADDR_NUM_LITERALS_DATA) / 4];
    img.num_clauses = s->regs[(C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_INITIALCLAUSECOUNT_DATA) / 4];
    img.clause_elements = s->regs[(C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_INITIALCLAUSEELEMENTS_DATA) / 4];
    uint64_t decay = reg64(s, P + XPQHANDLER_CONTROL_ADDR_DECAY_DATA);
    memcpy(&img.cfg.decay_factor, &decay, 8);
    img.clause_store = (int32_t *)mem_at(s, reg64(s, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_CLAUSESTORE_DATA));
    img.cmd = (uint32_t *)mem_at(s, reg64(s, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_CMD_DATA));
    img.cls_states = (int32_t *)mem_at(s, reg64(s, S + XSOLVER_CONTROL_ADDR_CLSSTATES_DATA));
    img.lit_store = (int32_t *)mem_at(s, reg64(s, S + XSOLVER_CONTROL_ADDR_LITSTORE_DATA));
    int32_t *ans = (int32_t *)mem_at(s, reg64(s, S + XSOLVER_CONTROL_ADDR_ANSWERSTACK_DATA));
    img.answer_stack = ans;
    img.lmd = mem_at(s, reg64(s, S + XSOLVER_CONTROL_ADDR_LMD_DATA));
    int32_t *misc = (int32_t *)mem_at(s, reg64(s, S + XSOLVER_CONTROL_ADDR_MISCCOUNTERS_DATA));
    memcpy(img.misc, misc, sizeof img.misc);

    int8_t *model = calloc(img.num_vars, 1);
    rl_sataccel_result r;
    int st = rl_sataccel_solve_image(&img, model, 600.0, &r);
    /* What solver() writes back (solver.cpp): counters, status, answer stack */
    misc[0] = (int32_t)r.iterations;
    misc[1] = (int32_t)r.decisions;
    misc[2] = (int32_t)r.retries;
    misc[3] = (int32_t)r.backtracks;
    misc[4] = (int32_t)r.restarts;
    misc[6] = st == 1 ? 1 : st == 0 ? 0 : 16;
    if (st == 1)
        for (uint32_t v = 0; v < img.num_vars; v++) ans[v] = model[v] > 0 ? (int32_t)(v + 1) : -(int32_t)(v + 1);
    free(model);
    static const uint32_t k[] = {SA_KERNEL_BASE_MESSAGE, SA_KERNEL_BASE_CLAUSE_STORE, SA_KERNEL_BASE_LOCATION,
                                 SA_KERNEL_BASE_TIMER, SA_KERNEL_BASE_RESTART, SA_KERNEL_BASE_PQ, S};
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) s->regs[k[i] / 4] = SA_AP_DONE | SA_AP_IDLE;
    s->regs[(S + XSOLVER_CONTROL_ADDR_ISR) / 4] = 1;
}

static void fake_wr32(void *ctx, uint32_t off, uint32_t v)
{
    sa_linux *s = ctx;
    if (off == SA_KERNEL_BASE_SOLVER && (v & SA_AP_START)) {
        run_solver(s);
        return;
    }
    if (v == 0x10 && (off & 0xFFFF) == 0) {          /* ap_continue: done -> idle */
        s->regs[off / 4] = SA_AP_IDLE;
        return;
    }
    if (off == SA_KERNEL_BASE_SOLVER + XSOLVER_CONTROL_ADDR_ISR) {   /* toggle on write */
        s->regs[off / 4] ^= v;
        return;
    }
    if ((off & 0xFFFF) == 0) return;                 /* other ap_start writes */
    s->regs[off / 4] = v;
}

static uint32_t fake_rd32(void *ctx, uint32_t off)
{
    sa_linux *s = ctx;
    if (off == SA_SHELL_BASE + SA_SHELL_ID) return SA_SHELL_ID_VALUE;
    if (off >= SA_SHELL_BASE) return 0;              /* counters */
    return s->regs[off / 4];
}

int sa_linux_open(sa_linux *s, const sa_caps *caps)
{
    memset(s, 0, sizeof(*s));
    s->fd = -1;
    s->regs_size = 1 << 20;
    s->regs = calloc(1, s->regs_size);
    s->mem_phys = FAKE_PHYS;
    s->mem_size = FAKE_MEM;
    s->mem = calloc(1, s->mem_size);
    if (!s->regs || !s->mem) return -1;
    sa_hw_ops ops;
    memset(&ops, 0, sizeof ops);
    ops.wr32 = fake_wr32;
    ops.rd32 = fake_rd32;
    ops.ctx = s;
    return sa_dev_init(&s->dev, &ops, s->mem_phys, s->mem, s->mem_size, caps);
}

void sa_linux_close(sa_linux *s)
{
    free((void *)s->regs);
    free(s->mem);
    s->regs = NULL;
    s->mem = NULL;
}

int sa_linux_solve(sa_linux *s, const sa_image *img, int used_only, size_t *bytes_loaded)
{
    size_t n = sa_dev_load(&s->dev, img, used_only);
    if (bytes_loaded) *bytes_loaded = n;
    if (n == (size_t)-1) return -1;
    sa_dev_start(&s->dev, img);
    return sa_dev_wait(&s->dev);
}

/* rl_board calls ioctl(S.fd, ...) directly; with fd = -1 it fails harmlessly. */
