/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Interface of /dev/sataccel, shared by the kernel driver and libsataccel. */
#ifndef SATACCEL_IOCTL_H
#define SATACCEL_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* Shell ID register (sataccel_hw/drv/sataccel_drv.h: SA_SHELL_BASE + SA_SHELL_ID) */
#define SATACCEL_SHELL_ID_OFF     0xF0000
#define SATACCEL_SHELL_ID         0x5A7A0001

/* DMA buffer: 32 MB covers SAT-Accel's buffers at the VCU118 capacities
 * (sa_dev_mem_required: about 17 MB) */
#define SATACCEL_DEFAULT_MEM_SIZE (32UL << 20)

/* mmap offsets: registers at 0, the DMA buffer at SATACCEL_MEM_PGOFF pages
 * (i.e. byte offset SATACCEL_MEM_PGOFF * page size) */
#define SATACCEL_MEM_PGOFF        0x1000UL

struct sataccel_info {
	__u64 mem_phys;     /* device (physical) address of the DMA buffer */
	__u64 mem_size;
	__u64 regs_size;
};

struct sataccel_wait {
	__u32 timeout_ms;
};

#define SATACCEL_IOC_MAGIC 'S'
#define SATACCEL_IOC_INFO  _IOR(SATACCEL_IOC_MAGIC, 1, struct sataccel_info)
#define SATACCEL_IOC_ARM   _IO(SATACCEL_IOC_MAGIC, 2)   /* before start: forget old interrupts */
#define SATACCEL_IOC_WAIT  _IOW(SATACCEL_IOC_MAGIC, 3, struct sataccel_wait)

#endif
