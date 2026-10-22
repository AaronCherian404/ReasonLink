| Run | T_conv (argmax + image) | T_xfer | T_sync | T_sat | Handoff share | us/puzzle (wall) | Bytes moved | Checks |
|---|---|---|---|---|---|---|---|---|
| serial_faithful | 1517.5 (1.06 + 1515.6) | 1252.9 | 0.2 | 146063.6 | 1.9% | 152079.6 | 10,536,436 | PASS (991+9 alt) |
| serial_lean | 797.4 (1.01 + 795.4) | 60.0 | 0.3 | 165435.8 | 0.5% | 168463.9 | 632,308 | PASS (991+9 alt) |
| double_faithful | 1623.8 (1.07 + 1622.3) | 1385.3 | 0.0 | 163404.8 | 0.0% | 166700.8 | 10,536,436 | PASS (991+9 alt) |
| double_lean | 769.4 (0.90 + 767.5) | 65.1 | 0.0 | 153693.5 | 0.0% | 156858.1 | 632,308 | PASS (991+9 alt) |

Times are medians in microseconds per puzzle. Handoff share is T_handoff / (T_handoff + T_sat) for serial runs and the exposed handoff fraction of wall time for double-buffered runs.
