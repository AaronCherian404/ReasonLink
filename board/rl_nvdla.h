/*
 * NVDLA classification of one Sudoku cell on the board: a C interface to
 * NVDLA's user-mode runtime (libnvdla_runtime, ESP's NV_NVDLA/sw), used by
 * the board baselines. Loads a loadable once, then each call fills the input
 * tensor the way nvdla_runtime does for a PGM (p -> round(p*127/255), int8,
 * at the tensor's own layout and strides), submits, and reads the int8
 * output (the ip2 logits with rl_lenet_ip2.nvdla).
 */
#ifndef RL_NVDLA_H
#define RL_NVDLA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rl_nvdla rl_nvdla;

/* NULL on failure (message on stderr). instance: NVDLA device index. */
rl_nvdla *rl_nvdla_open(const char *loadable_path, int instance);

/* Classify a 28x28 u8 image (white on black). out[10] = int8 outputs.
 * 0 on success. */
int rl_nvdla_classify(rl_nvdla *n, const uint8_t *pixels, int8_t *out);

void rl_nvdla_close(rl_nvdla *n);

#ifdef __cplusplus
}
#endif
#endif
