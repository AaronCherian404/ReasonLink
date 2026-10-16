// Minimal stand-in for Vitis HLS <hls_stream.h>: only the type is needed to
// compile SAT-Accel's headers on the host; the golden test never uses streams.
#pragma once
namespace hls {
template <typename T> class stream {};
}
