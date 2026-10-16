// Minimal stand-in for Vitis HLS <utils/x_hls_utils.h>. SAT-Accel's kernels
// use reg(); the host-side code in the golden test does not.
#pragma once
template <typename T> T reg(T x) { return x; }
