| Run | T_nn | T_conv | T_xfer | T_sync | T_sat | Handoff share | ms/puzzle (wall, no SSH stalls) | Bytes moved | Checks |
|---|---|---|---|---|---|---|---|---|---|
| board_double_delta | 2436898.5 | 149.0 | 339.4 | 2424838.3 | 12602.1 | -0.43% | 2470.7 (2469.9) | 3,940 | PASS (98+2 alt, NVDLA logit diffs 925) |
| board_double_delta_resident | 2435708.4 | 148.9 | 336.4 | 2431709.4 | 3323.1 | -0.06% | 2468.4 (2467.7) | 3,940 | PASS (98+2 alt, NVDLA logit diffs 926) |
| board_double_faithful | 2503527.8 | 722081.9 | 608418.8 | 3780636.4 | 13454.2 | 34.48% | 3944.0 (3866.9) | 5,293,556 | PASS (98+2 alt, NVDLA logit diffs 925) |
| board_double_lean | 2440548.6 | 412009.2 | 73031.2 | 2907804.5 | 12968.6 | 16.04% | 2961.7 (2957.3) | 632,436 | PASS (98+2 alt, NVDLA logit diffs 925) |
| board_double_reasonlink | 2440492.0 | 0.0 | 64.2 | 2427784.9 | 12621.3 | -0.45% | 2472.4 (2471.5) | 2,948 | PASS (98+2 alt, NVDLA logit diffs 925) |
| board_double_reasonlink_resident | 2435815.9 | 0.0 | 64.0 | 2432385.2 | 3314.9 | -0.08% | 2467.3 (2466.6) | 2,948 | PASS (98+2 alt, NVDLA logit diffs 926) |
| board_serial_delta | 2547761.2 | 147.7 | 335.6 | 562.5 | 10191.6 | 0.04% | 2591.7 (2506.4) | 3,940 | PASS (98+2 alt, NVDLA logit diffs 925) |
| board_serial_delta_resident | 2458728.0 | 146.6 | 336.4 | 545.5 | 3057.6 | 0.04% | 2494.5 (2493.9) | 3,940 | PASS (98+2 alt, NVDLA logit diffs 925) |
| board_serial_faithful | 2537826.3 | 721970.0 | 608999.6 | 723.3 | 10995.1 | 34.94% | 3977.7 (3878.1) | 5,293,556 | PASS (98+2 alt, NVDLA logit diffs 925) |
| board_serial_faithful_trace | 155.2 | 720134.4 | 607548.6 | 691.1 | 10979.5 | 99.17% | 1403.7 (1403.7) | 5,293,556 | PASS (98+2 alt, NVDLA logit diffs 0) |
| board_serial_lean | 2530492.3 | 411616.8 | 73198.1 | 550.6 | 10644.6 | 16.32% | 3037.2 (2994.2) | 632,436 | PASS (98+2 alt, NVDLA logit diffs 925) |
| board_serial_reasonlink | 2618510.6 | 0.0 | 59.9 | 552.4 | 10118.3 | 0.02% | 2820.9 (2502.2) | 2,948 | PASS (98+2 alt, NVDLA logit diffs 926) |
| board_serial_reasonlink_resident | 2615483.6 | 0.0 | 60.4 | 538.0 | 3004.9 | 0.02% | 2708.6 (2495.2) | 2,948 | PASS (98+2 alt, NVDLA logit diffs 925) |

T_* are medians in microseconds per puzzle. Handoff share: T_handoff / T_e2e for serial runs, the exposed handoff fraction of wall time for double-buffered runs. ReasonLink's per-cell push and credit wait (about 18 us per cell) are inside T_nn. A negative exposed handoff means the solve itself was hidden behind the next puzzle's NVDLA work (wall < sum T_nn + sum T_sat): nothing of the handoff is exposed. The no-SSH-stall wall time removes, per puzzle, the NVDLA time above the run's median per cell.
