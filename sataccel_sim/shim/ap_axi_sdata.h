// AXI-Stream side-channel struct, as in Vitis HLS <ap_axi_sdata.h>.
// Zero-width side channels are given one bit (they are never used).
//
// Fields are zero-initialised. SAT-Accel's sw_emu restartCalculator
// (restart.cpp) does `ap_axiu<1,0,0,0> pkt; stop.read_nb(pkt); if (pkt.data)
// break;`, so with uninitialised fields an empty stream reads as "stop" on
// stack garbage: the kernel exits early, the solver later consumes its final 0
// as a restart value, and the end-of-solve flush blocks forever. Zeroing makes
// an empty read_nb mean "no stop", matching the FPGA_HW path (blocking read).
#ifndef RL_SHIM_AP_AXI_SDATA_H
#define RL_SHIM_AP_AXI_SDATA_H

#include "ap_int.h"

template <int D, int U, int TI, int TD>
struct ap_axiu {
    ap_uint<D> data = 0;
    ap_uint<(D + 7) / 8> keep = 0;
    ap_uint<(D + 7) / 8> strb = 0;
    ap_uint<(U > 0 ? U : 1)> user = 0;
    ap_uint<1> last = 0;
    ap_uint<(TI > 0 ? TI : 1)> id = 0;
    ap_uint<(TD > 0 ? TD : 1)> dest = 0;
};

template <int D, int U, int TI, int TD>
struct ap_axis {
    ap_int<D> data = 0;
    ap_uint<(D + 7) / 8> keep = 0;
    ap_uint<(D + 7) / 8> strb = 0;
    ap_uint<(U > 0 ? U : 1)> user = 0;
    ap_uint<1> last = 0;
    ap_uint<(TI > 0 ? TI : 1)> id = 0;
    ap_uint<(TD > 0 ? TD : 1)> dest = 0;
};

#endif
