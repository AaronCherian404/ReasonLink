/*
 * Bare-metal test of the SAT-Accel tile (CU_SATACCEL) in an ESP SoC, for
 * full-system RTL simulation and first bring-up on the FPGA.
 *
 * It replays the register program the SAT-Accel driver core records for one
 * instance (sataccel_test.h, from sataccel_hw/tools/gen_baremetal_header.py)
 * after copying that instance's SAT-Accel memory image to the same physical
 * addresses, then checks the answer: SAT/UNSAT as expected, and for SAT
 * every clause satisfied. The tile runs in coherent-DMA mode
 * (ACC_COH_RECALL), set through its CSRs as in ESP's nv_nvdla_coh example.
 */
#include <stdio.h>
#ifndef __riscv
#include <stdlib.h>
#endif

#include <esp_accelerator.h>
#include <esp_probe.h>

#include "sataccel_test.h"

#define SLD_CU_SATACCEL 0x101
#define DEV_NAME "columbia,sataccel"

/* ESP tile CSRs (see nv_nvdla_coh) */
#define CSR_TILE_SIZE  0x200
#define CSR_BASE_ADDR  0x60090000
#define COH_REG_INDEX  99
#define SATACCEL_TILE  4      /* tile number in esp_soc/socs/.../esp_config */

#define SHELL_BASE     0xF0000
#define POLL_LIMIT     100000000u

static uint64_t counter(struct esp_device *dev, unsigned off)
{
    uint32_t lo = ioread32(dev, SHELL_BASE + off);   /* latches hi */
    uint32_t hi = ioread32(dev, SHELL_BASE + off + 4);
    return ((uint64_t)hi << 32) | lo;
}

int main(int argc, char *argv[])
{
    struct esp_device *espdevs;
    struct esp_device *dev;
    int ndev;

    printf("SAT-Accel bare-metal test: %s\n", SA_TEST_NAME);
    ndev = probe(&espdevs, VENDOR_SLD, SLD_CU_SATACCEL, DEV_NAME);
    if (ndev < 1) {
        printf("  SAT-Accel tile not found\n");
        return 1;
    }
    dev = &espdevs[0];

    uint32_t id = ioread32(dev, SHELL_BASE);
    printf("  shell ID 0x%08x\n", (unsigned)id);
    if (id != 0x5A7A0001) {
        printf("FAIL: unexpected shell ID\n");
        return 1;
    }

    /* Coherence mode: coherent DMA */
    unsigned int *coh = ((unsigned int *)CSR_BASE_ADDR) + (CSR_TILE_SIZE / sizeof(unsigned int)) * SATACCEL_TILE +
                        COH_REG_INDEX;
    *coh = ACC_COH_RECALL;

    /* Device memory: the recorded image at its recorded physical addresses */
    unsigned k = 0;
    for (unsigned r = 0; r < sizeof(sa_test_runs) / sizeof(sa_test_runs[0]); r++) {
        volatile uint64_t *p = (volatile uint64_t *)(uintptr_t)sa_test_runs[r][0];
        for (unsigned i = 0; i < sa_test_runs[r][1]; i++) p[i] = sa_test_data[k++];
    }
    esp_flush(ACC_COH_RECALL);
    printf("  image loaded: %u words\n", k);

    /* Replay the driver core's register program */
    for (unsigned i = 0; i < sizeof(sa_test_ops) / sizeof(sa_test_ops[0]); i++) {
        const uint32_t *op = sa_test_ops[i];
        if (op[0] == 0) {
            iowrite32(dev, op[1], op[2]);
        } else if (op[0] == 1) {
            unsigned n = 0;
            while ((ioread32(dev, op[1]) & op[2]) != op[3])
                if (++n == POLL_LIMIT) {
                    printf("FAIL: timeout polling 0x%05x\n", (unsigned)op[1]);
                    return 1;
                }
        } else {
            printf("  tile counters: busy=%llu mem_active=%llu mem_stall=%llu rd_beats=%llu wr_beats=%llu\n",
                   (unsigned long long)counter(dev, 0x18), (unsigned long long)counter(dev, 0x20),
                   (unsigned long long)counter(dev, 0x28), (unsigned long long)counter(dev, 0x30),
                   (unsigned long long)counter(dev, 0x38));
        }
    }

    /* Result: miscCounters[6] 1 SAT / 0 UNSAT; answerStack holds the model */
    volatile int32_t *misc = (volatile int32_t *)(uintptr_t)SA_TEST_MISC_ADDR;
    volatile int32_t *ans = (volatile int32_t *)(uintptr_t)SA_TEST_ANSWER_ADDR;
    int st = misc[6] == 1 ? 1 : misc[6] == 0 ? 0 : -1;
    static int8_t model[SA_TEST_NUM_VARS];
    int ok = st == SA_TEST_EXPECT;
    if (ok && st == 1) {
        for (int v = 0; v < SA_TEST_NUM_VARS; v++) model[v] = 0;
        for (int i = 0; i < SA_TEST_NUM_VARS && ok; i++) {
            int32_t l = ans[i];
            int v = (l < 0 ? -l : l) - 1;
            if (v < 0 || v >= SA_TEST_NUM_VARS || model[v]) ok = 0;
            else model[v] = l > 0 ? 1 : -1;
        }
        unsigned sat = 0;
        for (unsigned i = 0; ok && i < sizeof(sa_test_cnf) / sizeof(sa_test_cnf[0]); i++) {
            int32_t l = sa_test_cnf[i];
            if (l == 0) {
                if (!sat) ok = 0;
                sat = 0;
            } else if (model[(l < 0 ? -l : l) - 1] == (l > 0 ? 1 : -1)) {
                sat = 1;
            }
        }
    }
    printf("%s %s: result=%s expect=%s decisions=%d conflicts=%d restarts=%d\n", ok ? "PASS" : "FAIL",
           SA_TEST_NAME, st == 1 ? "SAT" : st == 0 ? "UNSAT" : "ERROR", SA_TEST_EXPECT ? "SAT" : "UNSAT",
           (int)misc[1], (int)misc[3], (int)misc[4]);
    return ok ? 0 : 1;
}
