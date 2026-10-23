// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2023.2 (64-bit)
// Tool Version Limit: 2023.10
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2023 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
// control
// 0x00 : Control signals
//        bit 0  - ap_start (Read/Write/COH)
//        bit 1  - ap_done (Read)
//        bit 2  - ap_idle (Read)
//        bit 3  - ap_ready (Read/COR)
//        bit 4  - ap_continue (Read/Write/SC)
//        bit 7  - auto_restart (Read/Write)
//        bit 9  - interrupt (Read)
//        others - reserved
// 0x04 : Global Interrupt Enable Register
//        bit 0  - Global Interrupt Enable (Read/Write)
//        others - reserved
// 0x08 : IP Interrupt Enable Register (Read/Write)
//        bit 0 - enable ap_done interrupt (Read/Write)
//        bit 1 - enable ap_ready interrupt (Read/Write)
//        others - reserved
// 0x0c : IP Interrupt Status Register (Read/TOW)
//        bit 0 - ap_done (Read/TOW)
//        bit 1 - ap_ready (Read/TOW)
//        others - reserved
// 0x10 : Data signal of clauseStore
//        bit 31~0 - clauseStore[31:0] (Read/Write)
// 0x14 : Data signal of clauseStore
//        bit 31~0 - clauseStore[63:32] (Read/Write)
// 0x18 : reserved
// 0x1c : Data signal of cmd
//        bit 31~0 - cmd[31:0] (Read/Write)
// 0x20 : Data signal of cmd
//        bit 31~0 - cmd[63:32] (Read/Write)
// 0x24 : reserved
// 0x28 : Data signal of usedClsIDBuckets
//        bit 31~0 - usedClsIDBuckets[31:0] (Read/Write)
// 0x2c : Data signal of usedClsIDBuckets
//        bit 31~0 - usedClsIDBuckets[63:32] (Read/Write)
// 0x30 : reserved
// 0x34 : Data signal of trackLBD
//        bit 31~0 - trackLBD[31:0] (Read/Write)
// 0x38 : Data signal of trackLBD
//        bit 31~0 - trackLBD[63:32] (Read/Write)
// 0x3c : reserved
// 0x40 : Data signal of initialClauseElements
//        bit 31~0 - initialClauseElements[31:0] (Read/Write)
// 0x44 : reserved
// 0x48 : Data signal of maxClauseElements
//        bit 31~0 - maxClauseElements[31:0] (Read/Write)
// 0x4c : reserved
// 0x50 : Data signal of initialClauseCount
//        bit 31~0 - initialClauseCount[31:0] (Read/Write)
// 0x54 : reserved
// 0x58 : Data signal of clausePageSize
//        bit 31~0 - clausePageSize[31:0] (Read/Write)
// 0x5c : reserved
// 0x60 : Data signal of prunePctage
//        bit 31~0 - prunePctage[31:0] (Read/Write)
// 0x64 : Data signal of prunePctage
//        bit 31~0 - prunePctage[63:32] (Read/Write)
// 0x68 : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_AP_CTRL                    0x00
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_GIE                        0x04
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_IER                        0x08
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_ISR                        0x0c
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_CLAUSESTORE_DATA           0x10
#define XCLAUSE_STORE_HANDLER_CONTROL_BITS_CLAUSESTORE_DATA           64
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_CMD_DATA                   0x1c
#define XCLAUSE_STORE_HANDLER_CONTROL_BITS_CMD_DATA                   64
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_USEDCLSIDBUCKETS_DATA      0x28
#define XCLAUSE_STORE_HANDLER_CONTROL_BITS_USEDCLSIDBUCKETS_DATA      64
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_TRACKLBD_DATA              0x34
#define XCLAUSE_STORE_HANDLER_CONTROL_BITS_TRACKLBD_DATA              64
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_INITIALCLAUSEELEMENTS_DATA 0x40
#define XCLAUSE_STORE_HANDLER_CONTROL_BITS_INITIALCLAUSEELEMENTS_DATA 32
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_MAXCLAUSEELEMENTS_DATA     0x48
#define XCLAUSE_STORE_HANDLER_CONTROL_BITS_MAXCLAUSEELEMENTS_DATA     32
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_INITIALCLAUSECOUNT_DATA    0x50
#define XCLAUSE_STORE_HANDLER_CONTROL_BITS_INITIALCLAUSECOUNT_DATA    32
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_CLAUSEPAGESIZE_DATA        0x58
#define XCLAUSE_STORE_HANDLER_CONTROL_BITS_CLAUSEPAGESIZE_DATA        32
#define XCLAUSE_STORE_HANDLER_CONTROL_ADDR_PRUNEPCTAGE_DATA           0x60
#define XCLAUSE_STORE_HANDLER_CONTROL_BITS_PRUNEPCTAGE_DATA           64

