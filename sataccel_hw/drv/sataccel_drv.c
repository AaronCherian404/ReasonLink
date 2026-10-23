/* See sataccel_drv.h. Mirrors SAT-Accel host.cpp lines 476-575 (buffers,
 * setArg, migrate, enqueueTask, wait), with XRT replaced by register writes.
 * Register offsets come from the Vitis HLS-generated x<kernel>_hw.h headers
 * (copied to hls_regs/ by sataccel_hw/Makefile). */
#include "sataccel_drv.h"

#include <string.h>

#include "xclause_store_handler_hw.h"
#include "xmessage_hw.h"
#include "xpqhandler_hw.h"
#include "xsolver_hw.h"

#define SA_AP_CONTINUE      0x10u
#define SA_CLAUSE_PAGE_SIZE 4u        /* configuration.json _HOST_CLAUSE_PAGE_SIZE */
#define SA_PRUNE_PERCENTAGE 0.1       /* configuration.json _HOST_PRUNE_PERCENTAGE */
#define SA_MAX_LBD_BUCKETS  10u       /* _FPGA_MAX_LBD_BUCKETS */
#define SA_POLL_LIMIT       (1u << 30)

static uint64_t align4k(uint64_t x) { return (x + 4095u) & ~(uint64_t)4095u; }

static void buf_sizes(const sa_caps *c, size_t s[SA_NUM_BUFS])
{
    s[SA_BUF_CLAUSE_STORE] = (size_t)c->max_clause_elements * 4;              /* cls */
    s[SA_BUF_CMD] = (size_t)c->max_clauses * 8;                                /* clauseMetaData */
    s[SA_BUF_USED_CLS_ID_BUCKETS] = (size_t)SA_MAX_LBD_BUCKETS * c->max_clauses * 4;
    s[SA_BUF_TRACK_LBD] = (size_t)SA_TRACK_LBD_WORDS * 4;
    s[SA_BUF_CLS_STATES] = (size_t)c->max_clauses * 8;                         /* clsStatePCIE */
    s[SA_BUF_LIT_STORE] = (size_t)c->max_literal_elements * 4;
    s[SA_BUF_ANSWER_STACK] = (size_t)c->max_vars * 4;                          /* host.cpp: numLiterals */
    s[SA_BUF_LMD] = (size_t)c->max_vars * SA_LMD_BYTES;                        /* host.cpp: numLiterals */
    s[SA_BUF_MISC] = 256 * 4;
    s[SA_BUF_DEBUG] = 8192 * 4;
}

size_t sa_dev_mem_required(const sa_caps *caps)
{
    size_t s[SA_NUM_BUFS], total = 0;
    buf_sizes(caps, s);
    for (int i = 0; i < SA_NUM_BUFS; i++) total = align4k(total) + s[i];
    return align4k(total);
}

int sa_dev_init(sa_dev *d, const sa_hw_ops *hw, uint64_t mem_phys, void *mem_virt,
                size_t mem_size, const sa_caps *caps)
{
    memset(d, 0, sizeof(*d));
    d->hw = *hw;
    d->mem_phys = mem_phys;
    d->mem_virt = mem_virt;
    d->mem_size = mem_size;
    if (mem_size < sa_dev_mem_required(caps) || ((mem_phys + mem_size - 1) >> 32))
        return -1;                                     /* 32-bit device addresses */
    buf_sizes(caps, d->buf_size);
    d->max_vars = caps->max_vars;
    uint64_t off = 0;
    for (int i = 0; i < SA_NUM_BUFS; i++) {
        off = align4k(off);
        d->buf_off[i] = off;
        off += d->buf_size[i];
    }
    return 0;
}

static void wr(sa_dev *d, uint32_t off, uint32_t v) { d->hw.wr32(d->hw.ctx, off, v); }
static uint32_t rd(sa_dev *d, uint32_t off) { return d->hw.rd32(d->hw.ctx, off); }

static int poll(sa_dev *d, uint32_t off, uint32_t mask, uint32_t val)
{
    if (d->hw.poll) return d->hw.poll(d->hw.ctx, off, mask, val);
    for (uint32_t i = 0; i < SA_POLL_LIMIT; i++)
        if ((rd(d, off) & mask) == val) return 0;
    return -1;
}

static uint64_t buf_addr(const sa_dev *d, int b) { return d->mem_phys + d->buf_off[b]; }

static void wr64(sa_dev *d, uint32_t off, uint64_t v)
{
    wr(d, off, (uint32_t)v);
    wr(d, off + 4, (uint32_t)(v >> 32));
}

static void wr_double(sa_dev *d, uint32_t off, double x)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof bits);
    wr64(d, off, bits);
}

int sa_dev_probe(sa_dev *d)
{
    return rd(d, SA_SHELL_BASE + SA_SHELL_ID) == SA_SHELL_ID_VALUE ? 0 : -1;
}

/* Copy n bytes of src into buffer b (and tell the backend). */
static void put(sa_dev *d, int b, const void *src, size_t n)
{
    if (!n) return;
    if (d->mem_virt) memcpy(d->mem_virt + d->buf_off[b], src, n);
    if (d->hw.mem_written) d->hw.mem_written(d->hw.ctx, buf_addr(d, b), src, n);
}

size_t sa_dev_load(sa_dev *d, const sa_image *img, int used_only)
{
    /* sa_image regions -> device buffers (sa_regions order) */
    static const int dst[SA_NUM_REGIONS] = {
        SA_BUF_CLAUSE_STORE, SA_BUF_TRACK_LBD, SA_BUF_CMD, SA_BUF_LIT_STORE,
        SA_BUF_ANSWER_STACK, SA_BUF_LMD, SA_BUF_CLS_STATES, SA_BUF_MISC};
    if (img->num_vars > d->max_vars / 2) return (size_t)-1;
    sa_region rows[SA_NUM_REGIONS];
    sa_regions(img, rows);
    size_t total = 0;
    for (int r = 0; r < SA_NUM_REGIONS; r++) {
        size_t n = used_only ? rows[r].used : rows[r].allocated;
        if (n > d->buf_size[dst[r]]) n = d->buf_size[dst[r]];
        put(d, dst[r], sa_region_ptr(img, r), n);
        total += n;
    }
    d->num_vars = img->num_vars;
    d->num_clauses = img->num_clauses;
    return total;
}

void sa_dev_start(sa_dev *d, const sa_image *img)
{
    const uint32_t M = SA_KERNEL_BASE_MESSAGE, C = SA_KERNEL_BASE_CLAUSE_STORE,
                   P = SA_KERNEL_BASE_PQ, S = SA_KERNEL_BASE_SOLVER;

    /* message: debug buffer, debug off */
    wr64(d, M + XMESSAGE_CONTROL_ADDR_DEBUG_DATA, buf_addr(d, SA_BUF_DEBUG));
    wr(d, M + XMESSAGE_CONTROL_ADDR_ENABLEDEBUG_DATA, 0);

    /* clause_store_handler (host.cpp 521-529) */
    wr64(d, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_CLAUSESTORE_DATA, buf_addr(d, SA_BUF_CLAUSE_STORE));
    wr64(d, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_CMD_DATA, buf_addr(d, SA_BUF_CMD));
    wr64(d, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_USEDCLSIDBUCKETS_DATA, buf_addr(d, SA_BUF_USED_CLS_ID_BUCKETS));
    wr64(d, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_TRACKLBD_DATA, buf_addr(d, SA_BUF_TRACK_LBD));
    wr(d, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_INITIALCLAUSEELEMENTS_DATA, img->clause_elements);
    wr(d, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_MAXCLAUSEELEMENTS_DATA, img->caps.max_clause_elements);
    wr(d, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_INITIALCLAUSECOUNT_DATA, img->num_clauses);
    wr(d, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_CLAUSEPAGESIZE_DATA, SA_CLAUSE_PAGE_SIZE);
    wr_double(d, C + XCLAUSE_STORE_HANDLER_CONTROL_ADDR_PRUNEPCTAGE_DATA, SA_PRUNE_PERCENTAGE);

    /* pqHandler (531-532) */
    wr(d, P + XPQHANDLER_CONTROL_ADDR_NUM_LITERALS_DATA, img->num_vars);
    wr_double(d, P + XPQHANDLER_CONTROL_ADDR_DECAY_DATA, img->cfg.decay_factor);

    /* solver (534-541) */
    wr64(d, S + XSOLVER_CONTROL_ADDR_CLSSTATES_DATA, buf_addr(d, SA_BUF_CLS_STATES));
    wr64(d, S + XSOLVER_CONTROL_ADDR_LITSTORE_DATA, buf_addr(d, SA_BUF_LIT_STORE));
    wr64(d, S + XSOLVER_CONTROL_ADDR_ANSWERSTACK_DATA, buf_addr(d, SA_BUF_ANSWER_STACK));
    wr64(d, S + XSOLVER_CONTROL_ADDR_LMD_DATA, buf_addr(d, SA_BUF_LMD));
    wr64(d, S + XSOLVER_CONTROL_ADDR_MISCCOUNTERS_DATA, buf_addr(d, SA_BUF_MISC));

    /* Completion interrupt: solver ap_done */
    wr(d, S + XSOLVER_CONTROL_ADDR_IER, 1);
    wr(d, S + XSOLVER_CONTROL_ADDR_GIE, 1);

    /* enqueueTask order (host.cpp 552-558) */
    static const uint32_t order[] = {SA_KERNEL_BASE_MESSAGE, SA_KERNEL_BASE_CLAUSE_STORE,
                                     SA_KERNEL_BASE_LOCATION, SA_KERNEL_BASE_TIMER,
                                     SA_KERNEL_BASE_RESTART, SA_KERNEL_BASE_PQ, SA_KERNEL_BASE_SOLVER};
    for (unsigned i = 0; i < sizeof order / sizeof order[0]; i++) wr(d, order[i], SA_AP_START);
}

int sa_dev_wait(sa_dev *d)
{
    /* evt.wait() on the solver, then q.finish() for all (ap_ctrl_chain:
     * acknowledge each ap_done with ap_continue, as XRT does). */
    static const uint32_t order[] = {SA_KERNEL_BASE_SOLVER, SA_KERNEL_BASE_MESSAGE,
                                     SA_KERNEL_BASE_CLAUSE_STORE, SA_KERNEL_BASE_LOCATION,
                                     SA_KERNEL_BASE_TIMER, SA_KERNEL_BASE_RESTART, SA_KERNEL_BASE_PQ};
    int rc = 0;
    for (unsigned i = 0; i < sizeof order / sizeof order[0]; i++) {
        if (poll(d, order[i], SA_AP_DONE, SA_AP_DONE)) rc = -1;
        wr(d, order[i], SA_AP_CONTINUE);
    }
    wr(d, SA_KERNEL_BASE_SOLVER + XSOLVER_CONTROL_ADDR_ISR, 1);   /* clear (toggle on write) */
    /* migrate back: answerStack, miscCounters, trackLBD (host.cpp 575) */
    if (d->hw.mem_read_back) {
        static const int back[] = {SA_BUF_ANSWER_STACK, SA_BUF_MISC, SA_BUF_TRACK_LBD};
        size_t len[] = {(size_t)d->num_vars * 4, 256 * 4, SA_TRACK_LBD_WORDS * 4};
        for (int i = 0; i < 3; i++)
            d->hw.mem_read_back(d->hw.ctx, buf_addr(d, back[i]),
                                d->mem_virt ? d->mem_virt + d->buf_off[back[i]] : NULL, len[i]);
    }
    return rc;
}

int sa_dev_result_read(sa_dev *d, int8_t *model, sa_dev_result *r)
{
    memset(r, 0, sizeof(*r));
    if (!d->mem_virt) return r->status = -1;
    const int32_t *misc = (const int32_t *)(d->mem_virt + d->buf_off[SA_BUF_MISC]);
    const int32_t *ans = (const int32_t *)(d->mem_virt + d->buf_off[SA_BUF_ANSWER_STACK]);
    r->iterations = (uint32_t)misc[0];
    r->decisions = (uint32_t)misc[1];
    r->retries = (uint32_t)misc[2];
    r->backtracks = (uint32_t)misc[3];
    r->restarts = (uint32_t)misc[4];
    /* miscCounters[6]: 1 SAT, 0 UNSAT; left at the page size on the error path */
    r->status = misc[6] == 1 ? 1 : misc[6] == 0 ? 0 : -1;
    if (r->status == 1 && model) {
        uint32_t nv = d->num_vars;
        memset(model, 0, nv);
        for (uint32_t i = 0; i < nv; i++) {
            int32_t l = ans[i];
            uint32_t v = (uint32_t)(l < 0 ? -l : l) - 1;
            if (v >= nv || model[v]) return r->status = -1;   /* malformed or duplicate */
            model[v] = l > 0 ? 1 : -1;
        }
    }
    return r->status;
}

uint64_t sa_dev_counter(sa_dev *d, uint32_t off)
{
    uint32_t lo = rd(d, SA_SHELL_BASE + off);          /* latches hi */
    uint32_t hi = rd(d, SA_SHELL_BASE + off + 4);
    return ((uint64_t)hi << 32) | lo;
}
