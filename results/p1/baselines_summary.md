| Run | T_conv (argmax + image) | T_xfer | T_sync | T_sat | Handoff share | us/puzzle (wall) | Bytes moved | Checks |
|---|---|---|---|---|---|---|---|---|
| serial_faithful | 1111.5 (0.69 + 1110.8) | 879.9 | 0.1 | 728.4 | 73.6% | 2808.8 | 10,536,436 | PASS (992+8 alt) |
| serial_lean | 695.5 (0.46 + 695.1) | 13.3 | 0.2 | 726.0 | 50.5% | 1450.2 | 632,308 | PASS (992+8 alt) |
| double_faithful | 1461.3 (0.86 + 1458.2) | 1159.0 | 0.0 | 763.9 | 70.5% | 2702.8 | 10,536,436 | PASS (992+8 alt) |
| double_lean | 710.9 (0.49 + 710.5) | 16.1 | 0.0 | 743.1 | 5.1% | 800.6 | 632,308 | PASS (992+8 alt) |

Times are medians in microseconds per puzzle. Handoff share is T_handoff / (T_handoff + T_sat) for serial runs and the exposed handoff fraction of wall time for double-buffered runs.
