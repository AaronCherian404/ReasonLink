# Full-SoC failure of quinn: ESP ignores AXI write strobes (2026-11-03)

## Diagnosis (full-SoC simulation with an AXI trace, 2026-11-02, 4 h 24 min)

`vsim.tcl` with `SATACCEL_AXI_VCD` dumped the SAT-Accel tile's AXI master port;
`sataccel_hw/tools/check_axi_vcd.py` replayed it on a model of memory (the
bare-metal test's image plus every write).

| | result |
|---|---|
| Read beats differing from the image/model | **0** of 528 (no reads outside the image) |
| Result words written by SAT-Accel (strobes applied) | misc = [6, 5, 0, 1, 0, 16, 1, 2]: SAT, 5 decisions, 1 conflict (as in RTL); a valid model in answerStack |
| The same writes with the strobes ignored | misc = [0, 0, 0, 0, 0, 0, 0, 2]: exactly what the CPU read (UNSAT, 0 decisions, 0 conflicts, 0 restarts) |
| Write beats with partial strobes | 238 of 251 (`0f` 31, `f0` 30, `00` 177; all 64-bit beats) |

SAT-Accel computes the right answer from the right data. ESP's AXI-to-NoC
proxy (`rtl/sockets/proxy/axislv2noc.vhd`) never reads `WSTRB`: it writes the
bytes that `AWSIZE` and the address select, so every 64-bit beat writes 8
bytes. SAT-Accel's HLS ports store 32-bit values as 64-bit beats with one half
enabled and pad bursts with empty beats, which overwrite the neighbouring
words. The RTL tests missed it because `axi_mem` honoured the strobes.
(The planned non-coherent repeat did not run: the new bare-metal program had
not compiled, and ESP's make copied the old binary.)

## Fix: `esp_soc/accelerator/CU_SATACCEL/axi_wstrb_split.v`

In the tile wrapper, between SAT-Accel and ESP's socket: each beat becomes
single-beat writes of only its enabled bytes (naturally aligned 8/4/2/1-byte
transfers, which ESP supports); empty beats are dropped; the burst's B
response follows the last downstream B. SAT-Accel's netlists are unchanged;
only the SoC needs synthesis and implementation again.

## RTL check (xsim, `axi_mem +esp_wr`: strobes ignored as in ESP)

| Test | Without adapter | With adapter | Cycles before -> with adapter |
|---|---|---|---|
| quinn | **FAIL** (UNSAT, 0 decisions: the SoC symptom) | PASS, 5 dec / 1 confl | 3,948 -> 4,510 (+14%) |
| aalto | | PASS, 6 / 0 | 3,114 -> 3,632 (+17%) |
| aalto_2 (UNSAT) | | PASS, 9 / 3 | 3,702 -> 4,116 (+11%) |
| aim50 | | PASS, 61 / 23 | 29,046 -> 29,856 (+2.8%) |
| dubois20 (UNSAT) | | PASS, 352 / 175 | 166,436 -> 166,850 (+0.2%) |
| nqueens_8 | | PASS, 30 / 10 | 33,778 -> 34,714 (+2.8%) |
| Sudoku 1 | | PASS, grid = reference | 203,534 -> 209,788 (+3.1%) |

Searches are identical to the strobe-honouring runs; no strobe-dependent beat
reaches memory with the adapter (`strobe-dependent beats=0`). The cost is in
the testbench's memory model; ESP's per-write NoC latency is larger.

Reproduce (torino, `sataccel_hw/`): `make sim-setup`, then
`ESP_WR=1 [SPLIT=1] bash tools/run_rtl_tests.sh <out> cnf:<file>:<1|0> sudoku:1`
(script: `~/m2_split.sh`).
