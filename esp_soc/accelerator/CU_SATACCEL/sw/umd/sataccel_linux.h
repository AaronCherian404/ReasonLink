/*
 * libsataccel: the SAT-Accel driver core (sataccel_hw/drv) on Linux, over
 * /dev/sataccel (sw/kmd). Register accesses go through the mmap'ed 1 MB
 * window; buffers live in the driver's DMA memory (coherent-DMA tile, so no
 * cache maintenance).
 */
#ifndef SATACCEL_LINUX_H
#define SATACCEL_LINUX_H

#include "sataccel_drv.h"

typedef struct {
    int fd;
    volatile uint32_t *regs;
    size_t regs_size;
    uint8_t *mem;
    uint64_t mem_phys;
    size_t mem_size;
    unsigned wait_timeout_ms;   /* completion interrupt wait (default 60 s) */
    sa_dev dev;
} sa_linux;

/* Open /dev/sataccel and set up the driver core for the given capacities.
 * 0 on success, -1 (errno set) otherwise. */
int sa_linux_open(sa_linux *s, const sa_caps *caps);
void sa_linux_close(sa_linux *s);

/* One solve: load (T_xfer), start, wait. Returns sa_dev_wait's result. */
int sa_linux_solve(sa_linux *s, const sa_image *img, int used_only, size_t *bytes_loaded);

#endif
