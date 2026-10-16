// Minimal stand-in for Vitis HLS <ap_axi_sdata.h> (type only).
#pragma once
#include "ap_int.h"
template <int D, int U, int TI, int TD> struct ap_axiu {
    ap_uint<D> data;
    ap_uint<1> last;
};
