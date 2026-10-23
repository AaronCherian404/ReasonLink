/* Host build: no NVDLA. rl_board runs with --nn trace (the emulator's logits). */
#include <stdio.h>

#include "rl_nvdla.h"

rl_nvdla *rl_nvdla_open(const char *loadable_path, int instance)
{
    (void)loadable_path;
    (void)instance;
    fprintf(stderr, "rl_nvdla: host build has no NVDLA; use --nn trace\n");
    return NULL;
}

int rl_nvdla_classify(rl_nvdla *n, const uint8_t *pixels, int8_t *out)
{
    (void)n;
    (void)pixels;
    (void)out;
    return -1;
}

void rl_nvdla_close(rl_nvdla *n) { (void)n; }

void *rl_nvdla_output(rl_nvdla *n, uint32_t *group, uint32_t *surf_stride)
{
    (void)n;
    *group = 32;
    *surf_stride = 0;
    return NULL;
}
