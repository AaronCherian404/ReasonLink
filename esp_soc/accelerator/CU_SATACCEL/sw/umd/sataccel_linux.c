/* See sataccel_linux.h. */
#define _GNU_SOURCE
#include "sataccel_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "sataccel_ioctl.h"

static void mmio_wr32(void *ctx, uint32_t off, uint32_t v)
{
    sa_linux *s = ctx;
    s->regs[off / 4] = v;
}

static uint32_t mmio_rd32(void *ctx, uint32_t off)
{
    sa_linux *s = ctx;
    return s->regs[off / 4];
}

/* The solver's completion raises the interrupt: sleep on it first, then
 * confirm in the register (other kernels finish right after, so plain
 * polling is fine for them). */
static int mmio_poll(void *ctx, uint32_t off, uint32_t mask, uint32_t val)
{
    sa_linux *s = ctx;
    if (off == SA_KERNEL_BASE_SOLVER && mask == SA_AP_DONE) {
        struct sataccel_wait w = {.timeout_ms = s->wait_timeout_ms};
        if (ioctl(s->fd, SATACCEL_IOC_WAIT, &w) && errno == ETIMEDOUT) return -1;
    }
    for (unsigned long i = 0; i < (1ul << 28); i++)
        if ((s->regs[off / 4] & mask) == val) return 0;
    return -1;
}

int sa_linux_open(sa_linux *s, const sa_caps *caps)
{
    memset(s, 0, sizeof(*s));
    s->wait_timeout_ms = 60000;
    s->fd = open("/dev/sataccel", O_RDWR | O_CLOEXEC);
    if (s->fd < 0) return -1;
    struct sataccel_info info;
    if (ioctl(s->fd, SATACCEL_IOC_INFO, &info)) goto fail;
    s->regs_size = info.regs_size;
    s->mem_phys = info.mem_phys;
    s->mem_size = info.mem_size;
    s->regs = mmap(NULL, s->regs_size, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
    if (s->regs == MAP_FAILED) goto fail;
    s->mem = mmap(NULL, s->mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd,
                  (off_t)SATACCEL_MEM_PGOFF * sysconf(_SC_PAGESIZE));
    if (s->mem == MAP_FAILED) goto fail;

    sa_hw_ops ops;
    memset(&ops, 0, sizeof ops);
    ops.wr32 = mmio_wr32;
    ops.rd32 = mmio_rd32;
    ops.poll = mmio_poll;
    ops.ctx = s;
    if (sa_dev_init(&s->dev, &ops, s->mem_phys, s->mem, s->mem_size, caps)) {
        errno = ENOMEM;
        goto fail;
    }
    if (sa_dev_probe(&s->dev)) {
        errno = ENODEV;
        goto fail;
    }
    /* Device-only buffers start zeroed, as XRT's fresh allocations are */
    memset(s->mem, 0, sa_dev_mem_required(caps));
    return 0;
fail:
    sa_linux_close(s);
    return -1;
}

void sa_linux_close(sa_linux *s)
{
    if (s->mem && s->mem != MAP_FAILED) munmap(s->mem, s->mem_size);
    if (s->regs && s->regs != MAP_FAILED) munmap((void *)s->regs, s->regs_size);
    if (s->fd > 0) close(s->fd);
    s->mem = NULL;
    s->regs = NULL;
    s->fd = -1;
}

int sa_linux_solve(sa_linux *s, const sa_image *img, int used_only, size_t *bytes_loaded)
{
    size_t n = sa_dev_load(&s->dev, img, used_only);
    if (bytes_loaded) *bytes_loaded = n;
    if (n == (size_t)-1) return -1;
    ioctl(s->fd, SATACCEL_IOC_ARM);
    sa_dev_start(&s->dev, img);
    return sa_dev_wait(&s->dev);
}
