# ReasonLink progress log

Running record of what is built, what was decided, and what is blocked. Update at the end of every stage.

**Current target:** P2: everything up to the VCU118 board run, without Vitis (v++/XRT), Vitis HLS only (see "P2 hardware plan"). P1 (Oct 19) is done.
**Branch:** `p2` (also merged into `main`), pushed to `github.com/AaronCherian404/ReasonLink` (public). torino uses a clone of it at `~/ReasonLink`; ESP at `~/esp` (tag 2026.1.0).

## Status at a glance (2026-11-05)

| Item | State |
|---|---|
| SAT-Accel as RTL without Vitis (P2-2, P2-3) | **Done.** 7 HLS kernels + block design + shell + driver core; 16/16 RTL tests pass; netlist 112k LUT / 434 URAM, meets 10 ns |
| SAT-Accel in an ESP SoC (P2-4) | **Builds end to end**: tile, Linux driver, bare-metal test, 2x3 VCU118 SoC, Linux image with both accelerators. Full-SoC simulation found a **real bug**: the first kernel-register read hangs (ESP's APB reads have no setup phase; our APB→AXI-Lite bridge waited for one). **Fixed** (2026-10-30), unit-tested; SoC simulation rerunning. **Bitstreams built (2026-10-31):** `top.bit` (SAT-Accel) and `top_rl.bit` (+ ReasonLink), both with Milestone 2; impl_rl meets timing (WNS +0.036 ns), impl_1 misses by 59 ps on 4 endpoints inside ESP's Ariane FPU; SAT-Accel's domain has +2.5 ns in both (`results/p2/m2/bitstreams.md`, P2-4 addendum). **Second real bug (2026-11-03):** the full-SoC simulation fails quinn because ESP's AXI socket ignores write strobes; SAT-Accel's results get overwritten. **Fixed** with a write adapter in the tile wrapper, verified in RTL (7/7 with ESP-like writes; without it quinn fails exactly as in the SoC) (`results/p2/m2/wstrb_fix.md`); the full-SoC simulation with it passes quinn (coherent DMA). **Bitstreams rebuilt (2026-11-04)** with the fix and the new board's address: both meet timing on every endpoint (`top.bit` WNS +0.774 ns, `top_rl.bit` +0.369 ns, WHS +0.009 ns). **On the board (2026-11-04/05, `top_rl.bit`):** bare-metal quinn PASS; Linux runs NVDLA + SAT-Accel end to end; four bugs in our software found and fixed (`results/p2/m2/board_bringup.md`) |
| Board baselines (P2-5) | **All 13 configurations done on the board (2026-11-05, 100 puzzles, `results/p2/board/`).** Serial: faithful, lean, delta, delta + resident, ReasonLink, ReasonLink + resident, and NVDLA replaced by the emulator's logits (trace); all correct on 100/100 (98 equal PySAT's grid, 2 valid alternatives) with identical grids, SAT/UNSAT and decision counts. H1: the faithful handoff is 34% of the run; with the delta or ReasonLink it is under 0.05%, and NVDLA (79 ms per cell under Linux) is the rest. Double-buffered: all six correct; the solve hides behind NVDLA, but the faithful and lean handoffs stay exposed (34.5%, 16.0%), H2's case. NVDLA hung twice mid-task (first attempts, reran clean) |
| ReasonLink (P2-6) | **Milestone 1 (phase B) passes in RTL** (puzzles 0, 3, 5 end to end; unit test); netlist and bitstream built. **Milestone 2 (phase A, resident base) passes in the functional model** (1000/1000 Sudoku, 192/192 benchmark instances, on-chip base byte-identical to a fresh copy-in) **and in RTL** (puzzles 0, 3, 5 back to back: resident solves take 51-57% fewer SAT-Accel cycles and 74% fewer DRAM reads, identical searches); both netlists with it meet 10 ns; **both bitstreams built**. **On the board, ReasonLink first decoded every given 8 and 9 wrongly** (it read channels 8, 9 from channels 0, 1: NVDLA's surface stride is 8 B, the LAYOUT register holds multiples of 256 B; all 81 satisfiable puzzles wrong). **Fixed in the driver** (contiguous layout; no new bitstream): 0 wrong literals on puzzles 0-5; then 98/100 serial and 98/100 with the resident base, answers identical to the baseline on 100/100; end to end it ties with the CPU delta (250.2 vs 250.7 s; 249.5 vs 249.4 s resident): both handoffs are under 1 ms per puzzle next to NVDLA's 2.5 s |
| Key measurements | Image delta per puzzle: 1.2 KB of 632 KB. Givens as fixed decisions: 1000/1000 match. RTL Sudoku solve: about 203k cycles, mostly copy-in memory stalls (testbench memory model). ReasonLink: about 67 cycles/cell conversion, 1,200 cycles delta write. **Board (100 puzzles, serial, wall time without SSH stalls):** faithful 387.8 s, lean 299.4 s, delta 250.7 s, delta + resident 249.4 s, ReasonLink 250.2 s, ReasonLink + resident 249.5 s; NVDLA replaced by its logits 140.4 s. NVDLA 79.3 ms per cell; faithful handoff 34% of the run; resident base: T_sat 10.2 to 3.1 ms; base.cnf solve 947k-955k cycles at 78.1 MHz |

Next steps:
1. **Write-strobe fix, end to end.** (a) Done 2026-11-03: the full-SoC simulation with the fix **passes quinn in coherent DMA** (SAT, 5 decisions, 1 conflict, as in RTL; AXI trace clean, `results/p2/m2/wstrb_fix.md`). The non-coherent repeat was stopped in a slow LLC flush and skipped (not a requirement: we use coherent DMA). (b) Done 2026-11-04 01:37 (`~/m2_rebuild.sh`, 6 h 28 min): `top.bit` (md5 9b3197f0ec0e06eff5d25f354900ba7e, WNS +0.774 ns) and `top_rl.bit` (md5 192946a19dbca44683fa6ecf6228f958, WNS +0.369 ns), all endpoints met; rebuilt both bitstreams with the fix and the new board's Ethernet address (`esp_defconfig`: IP C0A80111, MAC A6A7A0F80444; `install.sh` writes the lab's hw_server/UART/ESPLink settings into the SoC Makefile): SoC synthesis plus impl_1 and impl_rl, SAT-Accel's netlists unchanged.
2. **Linux image: done 2026-11-04** (`~/m2_linux.sh`: `make CU_SATACCEL && make -j8 linux`): `linux.bin` md5 07947174f77eb8824db2917d3b5f1810, the new MAC in the device tree, the initramfs's `rl_board` (with `--resident`) byte-identical to the new build. Bare-metal test rebuilt for coherent DMA only.
3. Board (new FPGA, see 1b), with `top_rl.bit` only (it also runs the unmodified SAT-Accel flow, so it covers every comparison): program it (`vivado/program.tcl` with impl_rl's bit; ESP's `fpga-program` always takes impl_1's), `TEST_PROGRAM=.../sataccel.exe make fpga-run` (bare-metal quinn), then `make fpga-run-linux`, `run_baselines.sh`, `summarize_board.py`. **Board up 2026-11-04** (the lab ports answer again). Bare-metal quinn **PASS on the FPGA** (SAT, 5 decisions, 1 conflict, as in RTL), so the write-strobe fix works in hardware; NVDLA reproduces ESP's seven.pgm output. Linux bring-up found three bugs in our software, all fixed (`results/p2/m2/board_bringup.md`):
   - (i) the bare-metal test enabled the solver interrupt with no handler; ESP's level-interrupt socket then waits forever for a PLIC complete and blocks every later SAT-Accel interrupt (Linux saw none; each solve waited out the 60 s timeout). The test no longer enables it; the UMD falls back to polling after 1 s (`SATACCEL_WAIT_MS`), and `run_baselines.sh` checks the interrupt.
   - (ii) the kernel ISR wrote the toggle-on-write ISR register; a re-sent interrupt set it again and hung the CPU. The ISR now clears GIE (idempotent, read back). One interrupt per solve; base.cnf solve 951k cycles (was 4.69e9).
   - (iii) `sataccel.ko`'s DMA pool and NVDLA's (`dma_declare_coherent_memory`) cover the same reserved region and both started at 0xa0200000: zeroing SAT-Accel's buffers wiped NVDLA's task descriptors (NULL tasks, then a hung submit). A 256 MB guard moves our buffer to 0xb0200000. `rl_board` then runs NVDLA + SAT-Accel end to end (2/2 grids correct).
   Also: ESP's NVDLA driver logs ~300 lines per task; on the serial console that is about 4 s per inference, so `run_baselines.sh` sets `dmesg -n 1` and stops klogd. NVDLA must stay in coherent DMA (mode 0 gives wrong seven.pgm outputs in 2 of 3 runs).
4. **Board run (2026-11-05):** the first `run_baselines.sh 100` (night of 11-04) lost SSH after two configurations, and its results were lost with the reboot. Rerun: serial faithful, serial lean and double-buffered faithful finished; double-buffered lean hung (NVDLA stalled). The rest ran in batches (`~/m2/rl_batch.sh`): serial delta, delta + resident, ReasonLink, ReasonLink + resident, trace. ReasonLink was wrong on every satisfiable puzzle; the `RL_DEBUG` dump found the cause (bug 4 in `board_bringup.md`), fixed in `sa_rl_setup`. Results: P2-5 "Board run" below.
   After the fix (pushed into the board's RAM as `/root/rl_board_fix`): serial ReasonLink 98/100, ReasonLink + resident 98/100, answers identical to the baseline. They first looked 9% slower than the CPU delta; `RL_TIMING` runs showed the extra time was occasional 1-3 s stalls from my SSH polling of the board, not ReasonLink (0 stalls in a 20-puzzle run without SSH). Corrected, the two tie.
5. Next: (a) done: ReasonLink's "7 ms per cell" was the SSH stalls; (b) done: double-buffered lean, delta, delta + resident, ReasonLink, ReasonLink + resident in one boot, all correct, no hang; (c) done: `summarize_board.py` (passes runs on their answers, removes SSH stalls; `results/p2/board/csv/board_summary.md`); (d) no separate board results note (user, 2026-11-05): the results live here and in `results/p2/board/`; (e) rebuild the Linux image so the initramfs `rl_board` has the layout fix and the debug switches (a long torino job: ask first; user: not yet); (f) the pipeline is NVDLA-bound and CPU-relayed per cell, not yet a good producer-consumer pipeline: see "Pipeline: what still needs to be optimized or figured out" below, with the data still missing.
6. Optional, not planned: restore clause states and literal metadata on chip as well (the remaining 145 KB of copy-in). No H3.

## P1 definition (adapted to ESP + NVDLA)

The proposal's P1 calls for three things. On the new platform they mean:

1. **The neural accelerator runs the classifier.** ESP's LeNet with retrained weights is compiled to an NVDLA nv_small loadable on torino. A bit-validated int8 emulator produces per-cell score traces, which stand in for NVDLA's output buffer in DRAM. *(Done in Stage 1.)*
2. **The SAT solver solves Sudoku CNFs in simulation.** There is no `v++`/XRT on torino, so per decision option C, MiniSat is the functional stand-in. It consumes the exact SAT-Accel memory image the host builds, which is verified byte for byte against SAT-Accel's own code. *(Done in Stage 2.)*
3. **Both baselines run end to end with cycle counters.** *(Done in Stage 3; results below.)* The serial and double-buffered handoffs follow SAT-Accel's native host flow:
   1. scores
   2. argmax
   3. full memory image
   4. transfer
   5. solve
   6. check

   Each run is decomposed into T_conv, T_xfer, T_sync and T_solve.

**Out of scope for P1:**
- the ReasonLink RTL module
- the system model
- NVDLA running on an ESP SoC
- VCU118 board work

**P1 report:** `Project Updates/P1 Exploration and Progress/neural-symbolic-p1.tex` (IEEEtran; two pages of main content plus references on page three). Build it with `latexmk -pdf neural-symbolic-p1.tex` from that milestone folder. It covers the project direction, software and RTL findings, implementation progress, blockers, and next steps. Figure sources and reproduction commands are in `figures/` and the milestone README; auxiliary files are kept in `build/`.

## Stage status

| Stage | Scope | Status | Commit |
|---|---|---|---|
| 0 | Repo scaffolding, local env, SAT-Accel submodule, server inventory, ESP clone (the toolchain is preinstalled on the server) | **Done** (2026-10-16) | see `git log` |
| 1 | Sudoku CNF, dataset, int8 LeNet + score traces, NVDLA compiler flow, pysat reference | **Done** (2026-10-16) | see `git log` |
| 2 | Standalone SAT-Accel image builder + byte-exact golden test; SAT solver on Sudoku (P1 stand-in: MiniSat reading the SAT-Accel image) | **Done** (2026-10-16) | see `git log` |
| 3 | Serial + double-buffered baselines end to end with cycle counters; RISC-V build | **Done** (2026-10-16). RISC-V build verified locally (Ubuntu cross compiler + QEMU) and built on torino. | see `git log` |
| P2-1 | SAT-Accel's kernels in functional simulation; cross-checked against Vitis HLS 2023.2 headers; `--solver sataccel` in the baselines | **Done** (2026-10-21) | branch `p2` |
| P2-2 | SAT-Accel to RTL with Vitis HLS only (no v++): capacity patch, 7 kernels synthesized for xcvu9p | **Done** (2026-10-22) | branch `p2` |
| P2-3 | Replace the v++ linker and XRT: block design, shell RTL, driver core, standalone RTL testbench, out-of-context netlist | **Done** (2026-10-22): 16/16 RTL tests pass; synthesized netlist meets 10 ns | branch `p2` |
| P2-4 | SAT-Accel as an ESP third-party tile; Linux driver + library; bare-metal test; VCU118 SoC (Ariane, NVDLA, SAT-Accel); Linux image; full-SoC simulation; bitstream | **Done** (2026-11-05): full-SoC simulation passes quinn (APB read and write-strobe bugs fixed); both bitstreams meet timing; on the VCU118, bare-metal quinn passes and Linux runs NVDLA + SAT-Accel (four software bugs fixed, `results/p2/m2/board_bringup.md`) | branch `p2` |
| P2-5 | Board baselines (Ariane, real NVDLA + SAT-Accel): rl_board, run script, summary | **Board runs done** (2026-11-05): all 13 configurations, 100 puzzles, all correct (`results/p2/board/`); results note to write | branch `p2` |
| P2-6 | ReasonLink: Milestone 1 / phase B (DRAM-level delta, hardware literal producer), then Milestone 2 / phase A (resident on-chip base) | **Milestone 1 passes in RTL**; **Milestone 2 passes in the functional model and in RTL**; both bitstreams built; on the board, the NVDLA layout bug was found and fixed in the driver (reruns below) | branch `p2` |

## Stage 1: workload and classifier

### Reproduce (local WSL, venv `~/venvs/reasonlink`)
Run from `workload/`. The whole sequence takes about 1.5 min on a laptop.

```bash
bash fetch_esp_lenet.sh                     # ESP's NVDLA LeNet assets -> data/nvdla (md5-checked)
python nvdla_lenet.py --model esp --check   # int8 emulator vs ESP's NVDLA output
python train_lenet.py                       # retrain -> models/rl_lenet/ (committed)
python dataset.py --num 1000 --seed 0       # -> data/sudoku/{base.cnf,puzzles.npz,trace.bin}
python reference_solve.py                   # -> results/p1/reference_*.{csv,json}, data/sudoku/reference.npz
```

### Classifier: ESP's LeNet architecture with retrained weights

**The int8 emulator matches NVDLA bit for bit on ESP's reference.** `workload/nvdla_lenet.py` loads Caffe weights with a small protobuf reader and emulates the int8 pipeline:
- input: the runtime's own mapping, `round(p·127/255)`
- per-channel int8 weights
- int32 accumulation
- requantization to each layer's calibrated scale, where ip1 uses the `relu1` scale
- softmax on the CPU, quantized at about 1/127

With ESP's weights, the output for `seven.pgm` is `[0,2,0,0,0,0,0,124,0,0]`, identical to ESP's published NVDLA `--rawdump`.

**ESP's demo weights are not usable for Sudoku.** They reach only **74.5%** on (inverted) MNIST and 13% on standard MNIST; for example, 1 of 974 eights is classified correctly. The cause is in the caffemodel itself: it was trained in NVIDIA DIGITS with mean-image subtraction (`mean.binaryproto`) and a `Power` scale of 0.0125. The deploy prototxt drops both, and the NVDLA runtime's int8 input path (`TestUtils.cpp`, `createImageCopy`) cannot apply them, because it ignores `--mean` and `--normalize` for int8.

**The fix.** `workload/train_lenet.py` keeps ESP's exact prototxt and retrains the weights on `p/255`, white digits on black.
- The new `.caffemodel` is ESP's layer records with our weight blobs swapped in. Training-only layers are removed, and the bottom of conv1 is set to `data`.
- The calibration table uses ESP's JSON format with max-abs/127 scales. The data scale is 1/127, so the runtime's `round(p·127/255)` means exactly `p/255`.

| Model | MNIST test float | MNIST test int8 (emulated) |
|---|---|---|
| ESP demo weights | 74.5% (inverted input) | 74.5% |
| **Retrained (`models/rl_lenet`, seed 0, 8 epochs)** | **99.17%** | **99.20%** |

The exit criterion (float ≥ 99% and int8 within 0.5% of float) passes.

### NVDLA compiler on torino
**Reproduce:** on torino, run `cd ~/ReasonLink && bash workload/nvdla_compile.sh`. It recompiles ESP's model and checks the md5, then compiles ours to `build/nvdla/rl_lenet.nvdla`.

**Setup:**
- `NV_NVDLA/sw` submodule at `4c02fb4351efb43b3490c5720f21fd6d32f8abaa`.
- The prebuilt x86 `nvdla_compiler` runs on RHEL 8. It needs `LD_LIBRARY_PATH=$NVC` to find `libnvdla_compiler.so`, where `NVC=~/esp/accelerators/third-party/NV_NVDLA/sw/prebuilt/x86-ubuntu`.

**Options:**
- `--profile`, `--cprecision fp16|int8`, `--configtarget`, `--calibtable`
- `--quantizationMode per-kernel|per-filter` (default per-kernel)
- `--informat ncxhwx|nchw|nhwc` (default nhwc)
- Use `-h` for help; `--help` is rejected.

**Quirk.** `--calibtable` with an absolute path fails with a rapidjson `fp_ != 0` assertion. Copy the inputs into the working directory and pass bare file names.

**The compiler is deterministic** (the same flags always give the same md5).

**Reproducing ESP's `lenet_mnist.nvdla`** (md5 `9831533f…`, 445,736 B), all int8 on nv_small:

| `--quantizationMode` | `--informat` | md5 | Notes |
|---|---|---|---|
| **per-filter** | **nchw** | **`9831533f…` = ESP's loadable, byte for byte** | |
| per-filter | nhwc | `d51c8f3b…` | warns: input treated as 4-channel A8B8G8R8 |
| per-kernel (default) | nchw / ncxhwx | `e4d6150c…` | |
| per-kernel (default) | nhwc (default) | `727b01dc…` (446,096 B) | warns: 4-channel input |

So ESP built its demo with `--quantizationMode per-filter --informat nchw`. Our emulator already used per-output-channel weight scales: with them it reproduces ESP's `124`, whereas per-tensor scales give `125`.

**Our classifier loadable.** `rl_lenet.nvdla` was compiled with the same flags: **445,736 B, md5 `eb4c08138cee008fcb65e0cb828863b5`** (2026-10-16). It is a build output under `build/` (gitignored) and can be regenerated with the script above. It is ready to run with `nvdla_runtime --loadable rl_lenet.nvdla --image <cell>.pgm --rawdump` once NVDLA runs on ESP, after P1.

**Not yet verified:**
- `nvdla_runtime` output for `rl_lenet.nvdla` against our emulator. That needs NVDLA running, either on the board or in an ESP simulation with Linux.
- Whether softmax runs as a CPU emulator task. The compiler log only shows `mark prob` (the output marker).

### Sudoku workload
- **Encoding (`sudoku_cnf.py`).** Variables are numbered `var = 81r + 9c + d`, cell-major. The extended encoding gives **729 vars and 11,988 clauses** (324 at-least-one clauses plus 11,664 binary clauses), with 26,244 literal occurrences. The base formula is identical for every puzzle; the givens are unit literals.
- **Dataset (`dataset.py`, seed 0).** 1,000 puzzles, each with a unique solution and 24–40 clues (mean 32.1). Every given cell gets an MNIST *test* image of its digit and is classified by the int8 stand-in.
- **NVDLA output trace (`trace.bin`).** Per puzzle: an 81-byte given mask, then 81 × 16 B of int8 ip2 logits (10 used). The 16 B stride is our choice for the stand-in; NVDLA's real output surface layout is to be confirmed on ESP.

### Reference solve (pysat MiniSat 2.2, `results/p1/reference_summary.json`)

| Metric | Value |
|---|---|
| Given-cell accuracy (argmax over digits 1–9) | 99.26% |
| Puzzle accuracy (solution equals ground truth) | **79.0%** (≈ 0.9926^32) |
| UNSAT rate (misread givens contradict each other) | 19.2% |
| SAT but wrong | 1.8% |
| Solver work per puzzle (mean) | 2.0 decisions, 677 propagations, 0.16 conflicts, ~0.1 ms |
| Oracle (true givens) | always SAT and correct (asserted), 0.21 conflicts |

**What this means for the study.** With 24–40 clues, Sudoku is almost entirely solved by propagation. T_sat will be tiny, so the handoff is likely to dominate (H1). The H3 crossover will need harder instances: fewer clues, 16×16, or non-Sudoku CNFs.

## Stage 2: SAT-Accel memory image and SAT solving

### Reproduce (local WSL, after Stage 1's data exists)
```bash
cd handoff
make            # build/{image_tool,golden_parse,solver_check}
make golden     # byte-exact golden test   -> results/p1/golden_test.txt
make solvecheck # 1,000 puzzles end to end -> results/p1/solver_check.txt
```

### Image builder: C port of SAT-Accel's host conversion (`handoff/sataccel_image.{h,c}`)
`sa_build_image()` reproduces `parseDIMACS`'s image construction (`host.cpp` 71–376) from an in-memory CNF:
- de-duplication and |literal| sort of each clause
- 4-word clause pages with next-page links
- per-variable, per-polarity occurrence pages of 16 words
- `lmd` bit fields set at the `LMD_*` offsets
- `answerStack` = sorted decision set (unit literals plus unused variables)
- `miscCounters`

It also provides:
- `sa_regions()`: allocated vs. used bytes per region
- `sa_decode_clauses()`: the device-side view, used by the solver stand-in

Only one known difference from `host.cpp`: the clause sort is stable. Clauses containing both `x` and `-x` could therefore be ordered differently than by `host.cpp`'s `std::sort`. No test case has such clauses.

### Golden test: byte-exact against SAT-Accel's own code (`handoff/golden_test/`)
- **Reference build.** `parseDIMACS` is cut verbatim from the pinned `host.cpp` at build time (awk in `handoff/Makefile`). It is compiled unmodified with g++ against the open-source Xilinx `ap_int` headers, plus three small stub headers for the HLS stream and AXI types it never uses.
- **Comparison.** Both builders dump every region at full allocated size, and `cmp` checks them: `clause_store`, `cmd`, `lit_store`, `answer_stack`, `cls_states`, `misc`, `lmd`. For `lmd`, the golden side extracts bits with `range()`, so the result is independent of `ap_int`'s object layout. `track_lbd` is skipped because `host.cpp` never initializes it.
- **Result: 13/13 PASS** (`results/p1/golden_test.txt`):
  - 3 SAT Sudoku instances and 1 UNSAT Sudoku instance (729 vars, about 12,020 clauses)
  - SAT-Accel's own benchmarks: `aim50`, `nqueens_4`, `quinn`, `4blocks` (758 vars / 47,820 clauses), `dubois20`, `nqueens_3`, `aalto_2`, `hole7_unsat`, `aim-200-1_6-no-1`

### Bytes per Sudoku instance (puzzle 0, 36 givens; `image_tool`)

| Region | Allocated (migrated by host.cpp) | Used |
|---|---|---|
| clause_store | 4,194,304 | 202,752 |
| cmd | 1,048,576 | 96,192 |
| cls_states | 1,048,576 | 96,192 |
| lit_store | 4,194,304 | 186,624 |
| lmd | 46,656 | 46,656 |
| answer_stack | 2,916 | 2,916 |
| misc + track_lbd | 1,104 | 1,104 |
| **Total** | **10,536,436 (10.5 MB)** | **632,436 (632 KB)** |

**Implication for H1.** The neural stage contributes 36 (cell, digit) givens, or 81 × 16 B = 1,296 B of int8 logits. Yet SAT-Accel's native flow rebuilds and migrates 10.5 MB per puzzle, of which 632 KB carries data, and almost all of that is the unchanging base formula. This is the gap the serial baseline will measure, and the one delta transfers target.

### SAT solving on all 1,000 puzzles (`handoff/solver_check.c`)
For each puzzle, the full chain runs:
1. NVDLA trace
2. C argmax (digits 1–9, ties to the lowest)
3. unit literals `var = 9·cell + d`
4. SAT-Accel image (C port)
5. device-side decode
6. MiniSat

| Check | Result |
|---|---|
| Build or decode errors | 0 |
| C argmax givens == Python reference | 1000/1000 |
| SAT/UNSAT == pysat reference | 1000/1000 (808 SAT, 192 UNSAT) |
| Solved grid == pysat reference | 992/1000 |
| The 8 differing grids | **all valid alternative solutions.** Both grids satisfy every clause of their instance. Each of these puzzles has exactly one misread given, which breaks uniqueness, and the reference already counts it as wrong. |
| MiniSat work (mean) | 1.17 decisions, 675.7 propagations, 0.158 conflicts per puzzle |

**The stand-in is a different MiniSat build from pysat's `m22`.** On multi-solution instances it may return a different (valid) model, which the 8 differing grids reflect. Stage 3 runs every interface on this same backend, so solutions must be identical across interfaces.

## Stage 3: baselines end to end with cycle counters

### Reproduce (local WSL, after Stages 1–2)
```bash
cd baselines
make            # build/native/rl_baselines
make run        # 4 runs x 1,000 puzzles + checks -> results/p1/baseline_*.csv, baselines_summary.{json,md}
make riscv      # static RISC-V Linux binary for Ariane (torino toolchain)
make riscv RISCV_CROSS=riscv64-linux-gnu-   # same, with Ubuntu's g++-riscv64-linux-gnu
```

### What runs (`baselines/harness.c`)
Two threads: the **host** runs SAT-Accel's native flow, and a second thread stands in for the **device**. Every phase is timed with the cycle counter (`baselines/rl_timer.h`: `rdtsc` on x86, `rdcycle` on RISC-V).

| Phase | What it covers |
|---|---|
| T_conv | Argmax over the int8 logits → unit literals, then base CNF + units → SAT-Accel image (`sa_build_image`, Stage 2) |
| T_xfer | Copy every region into separate "device" buffers, then the scalar kernel arguments |
| T_sync | Doorbell (slot state → READY) plus detecting completion (busy-poll). `--poll-us` emulates sleep-polling; SAT-Accel's `host.cpp` polls every 1 s. |
| T_sat | Device thread: decode its copy of the image, MiniSat (P1 stand-in, option C), extract the grid |
| T_nn | **Not measured in P1.** The NVDLA output is replayed from `trace.bin`. |

**The two baselines:**
- **Serial:** one buffer set; the host waits for each solve.
- **Double-buffered:** two buffer sets with explicit ownership states (FREE → READY → DONE → FREE). The host converts and transfers puzzle *i+1* while the device solves puzzle *i*, and it stalls only when the device still owns the slot it needs.

**Configurations:**
- **Faithful:** clears the full allocated buffers and copies all 10.5 MB, as `host.cpp` does per instance.
- **Lean:** clears and copies only the used bytes (632 KB). It is there so the baseline is not a strawman.

All runs use 20 warm-up puzzles and then measure all 1,000.

### Results (WSL2 on a laptop, x86 `rdtsc`; `results/p1/baselines_summary.md`)
Medians per puzzle, in µs:

| Run | T_conv (argmax + image) | T_xfer | T_sync | T_sat (MiniSat stand-in) | Handoff share | Wall µs/puzzle | Bytes moved |
|---|---|---|---|---|---|---|---|
| serial, faithful | 1111.5 (0.69 + 1110.8) | 879.9 | 0.1 | 728.4 | **73.6%** | 2808.8 | 10,536,436 |
| serial, lean | 695.5 (0.46 + 695.1) | 13.3 | 0.2 | 726.0 | **50.5%** | 1450.2 | 632,308 |
| double, faithful | 1461.3 (0.86 + 1458.2) | 1159.0 | 0.0 | 763.9 | **70.5% exposed** | 2702.8 | 10,536,436 |
| double, lean | 710.9 (0.49 + 710.5) | 16.1 | 0.0 | 743.1 | **5.1% exposed** | 800.6 | 632,308 |

- **Handoff share.** For serial runs it is T_handoff / (T_handoff + T_sat). For double-buffered runs it is the exposed handoff, (wall − ΣT_sat) / wall.
- **Correctness.** Every run passes the checks:
  - SAT/UNSAT and grids are identical across all 4 runs.
  - Results equal the PySAT reference: 992 exact, plus the same 8 verified alternative solutions as in Stage 2.

### What the numbers say (pilot H1/H2, with host-CPU caveats)
- **H1 (serial).** The handoff is **74% of end-to-end time** in SAT-Accel's native flow, and still 51% with lean buffer handling.
  - The argmax, which is the step that actually consumes the neural output, takes under 1 µs.
  - Almost all of the cost is rebuilding and moving an image of the unchanging base formula.
- **H2 (double buffering).** It hides the handoff only when the handoff is shorter than the solve.
  - **Faithful:** the handoff is about 2.6 ms against a 0.76 ms solve, so overlap barely helps (2809 → 2703 µs/puzzle; 70.5% still exposed).
  - **Lean:** the handoff is about 0.73 ms against 0.74 ms, so it is almost fully hidden (5.1% exposed), but it still moves 632 KB per puzzle for 36 givens.
  - In the faithful double-buffered run, T_conv and T_xfer grow by about 30% over serial: host and device compete for memory bandwidth.

### Caveats
- **Host timing.** All times are host x86 cycles under WSL2. On ESP, conversion runs on Ariane and the transfer is DMA over ESP's NoC, so absolute numbers and ratios will change. This harness is the code that will run there.
- **T_sat is MiniSat, not SAT-Accel.** It includes MiniSat loading about 12k clauses, so it is a rough stand-in for SAT-Accel's copy-in plus search. Search itself is tiny (1.2 decisions per puzzle).
- **No outlier filtering.** These are single runs of 1,000 puzzles each.

### RISC-V build
- **Local.** `make riscv RISCV_CROSS=riscv64-linux-gnu-` builds a static RISC-V Linux executable (rv64gc, glibc). Run under `qemu-riscv64`, its serial and double modes produce SAT flags and grids identical to the native run on 50 puzzles, and `rdcycle` works.
- **torino (2026-10-16, commit `0eebc8a`).** `make riscv` with the user's toolchain at `~/riscv/bin/riscv64-unknown-linux-gnu-g++` (GCC 9.2) builds `build/riscv64/rl_baselines`: a 6.6 MB static RISC-V Linux ELF.
  - **First attempt failed:** MiniSat's `utils/ParseUtils.h` includes `<zlib.h>`, which that sysroot lacks. `make riscv` now adds `baselines/compat/` with a declarations-only stub. The gzip reader is never used, so nothing links against zlib.
  - **ISA flags:** `file` does not report RVC or the double-float ABI for this binary, unlike the Ubuntu rv64gc build. The toolchain's ISA/ABI defaults are to be checked against Ariane's configuration when it runs on ESP.

## P2 Stage 1: SAT-Accel in functional simulation (`sataccel_sim/`)

**Status: done (2026-10-21).** Functional check passes with our shim and with Vitis HLS 2023.2's own headers on torino, and SAT-Accel is a solver option in the baselines (`--solver sataccel`).

### Reproduce (local WSL, after P1 Stages 1–2)
```
cd sataccel_sim
make            # build/sataccel_check
make check      # about 8 min; writes results/p2/sataccel_{benchmarks.txt,sudoku.csv,sudoku.txt}
cd ../baselines
make run-sataccel   # about 12 min; the four P1 configurations with --solver sataccel -> results/p2
```
`HLS_INC=/path/to/Vitis_HLS/<ver>/include make` builds against vendor headers instead of `shim/`.

Vendor-header cross-check, run by the user on torino (output in `results/p2/sataccel_benchmarks_vitis2023.2.txt`):
```
cd ~/ReasonLink && git fetch origin && git checkout p2 && git pull --ff-only
git submodule update --init third_party/FPGA25_SAT_Accel third_party/HLS_arbitrary_Precision_Types
HLS_ROOT=/opt/Xilinx/Vitis_HLS/2023.2
cd sataccel_sim && make clean && make -k -j8 HLS_INC=$HLS_ROOT/include
BENCH_ONLY=1 OUT=/tmp/sa_p2 bash run_check.sh
```

### What runs
- **Kernels.** SAT-Accel's 16 kernel sources compiled unmodified, with `FPGA_HW` undefined (their sw_emu code path).
- **Execution.** The 7 kernels (solver, clause_store_handler, location_handler, restartCalculator, timer, pqHandler, message) run as threads, wired exactly as `k2k.cfg`'s `stream_connect` lines, as Vitis sw_emu does. Scalar arguments follow `host.cpp`'s `setArg` calls.
- **Input.** Our `sa_image` (the byte-exact port of SAT-Accel's host conversion), copied field by field into the kernels' buffer types.
- **HLS headers.** `shim/`: a thread-safe unbounded `hls::stream` with a blocked-read watchdog (`RL_STREAM_TIMEOUT_S`, default 30 s), `ap_axiu`, and Xilinx's open-source `ap_int` (submodule `third_party/HLS_arbitrary_Precision_Types`).
- **Checks.** Every SAT answer is checked against every clause. In Sudoku mode, SAT/UNSAT and the grid are also compared with the PySAT reference; a different grid counts only if it is itself a valid solution.

### Results (`results/p2/`)
| Set | Result |
|---|---|
| SAT-Accel's benchmarks (our P1 golden set plus `testcases_sim.sh` instances present in the repo) | **16/16 pass** (10 SAT, 6 UNSAT). Slowest: `hole7` (4,419 decisions, 3,558 conflicts, 24.7 s) and `4blocks` (11,649 decisions, 23.8 s). |
| Same benchmarks, built against **Vitis HLS 2023.2's headers** on torino | **16/16 pass**, with decision and conflict counts **identical** to the shim build on every instance (the search is deterministic, so this cross-validates the shim). |
| 1,000 Sudoku puzzles | SAT/UNSAT matches PySAT on **1000/1000**. Grids: 991 identical, 9 valid alternative solutions, 0 wrong. Mean 1.44 decisions and 0.14 conflicts per puzzle, 12 ms simulation time. |

Times are simulation wall time on the laptop, not hardware cycles. `sim` excludes the conversion of the image into the kernels' `ap_int` buffer types.

### Baselines with SAT-Accel as the solver (`results/p2/baselines_summary.md`)
- **Build.** `make sataccel` in `baselines/` links `sataccel_sim/build/libsataccel.a` and enables `--solver sataccel`; the default build and `make riscv` are unchanged (MiniSat).
- **Correctness.** All four configurations (serial/double × faithful/lean) **PASS**: SAT/UNSAT and grids identical across runs, and equal to the PySAT reference (991 identical, 9 valid alternatives).
- **Handoff.** T_conv, T_xfer and bytes moved match P1's (same host code): faithful 10.5 MB per puzzle, lean 632 KB.
- **T_sat is not meaningful as a time.** It is simulation time (about 150 ms per puzzle), dominated by converting the full allocated device buffers into `ap_int` types for the simulator. So the "handoff share" column in that table should not be read as a result; P1's MiniSat numbers remain the timing baseline until SAT-Accel runs in hw_emu/RTL or on the board.
- **Harness fix.** `sa_image.misc` (miscCounters) is an inline array, so the device's view kept the init-time copy; the SAT-Accel path now reads it from the transferred device buffer. MiniSat never reads it, so P1 results are unaffected.

### Problem found: uninitialised `read_nb` packet in SAT-Accel's sw_emu restart kernel
- **Symptom.** `4blocks`, `qg6-10` and `hole7` hung: `restartCalculator` and `pqHandler` had exited, and the solver was blocked reading `restartValueStream`.
- **Cause.** `restart.cpp`'s sw_emu path does `ap_axiu<1,0,0,0> pkt; stop.read_nb(pkt); if (pkt.data) break;`. When the stream is empty, `pkt.data` is uninitialised (`ap_uint`'s default constructor does not zero it). Stack garbage reads as "stop", so the kernel exits early and writes its final 0. The solver then consumes that 0 as a restart value at its next restart, and its end-of-solve flush (`FLUSH_RESTART_*`, waiting for a 0) blocks forever. Only instances that reach a restart were affected.
- **Fix.** `shim/ap_axi_sdata.h` zero-initialises all `ap_axiu`/`ap_axis` fields, so an empty `read_nb` means "no stop". This matches the `FPGA_HW` path, which stops on a blocking read of a real stop message. The kernel sources stay unmodified.
- **Vendor headers.** The Vitis HLS 2023.2 build does not hang on these instances and gives the same search trace, so Vitis's sw_emu types do not expose the bug.
- **Vendor include path.** Vitis HLS 2023.2 keeps `ap_utils.h` under `include/etc/`; the Makefile adds it when `HLS_INC` is set.

### Fidelity notes
- **Unbounded streams.** As in sw_emu, streams have no depth limit. Vitis's csim reports a maximum occupancy of up to 8,055 entries (`4blocks`), far above hardware FIFO depths (`k2k.cfg` sets 16 for `messageStream`). In hardware, a full FIFO stalls the producer; that changes timing, not answers, unless the protocol deadlocks under backpressure. Only hw_emu/RTL can rule that out.
- **No timing.** Simulation time says nothing about SAT-Accel's cycle count.

## P2 hardware plan (2026-10-22): everything up to the board run, without Vitis

**Scope (user, 2026-10-22).** Assume v++/XRT never become available; use Vitis HLS (and Vivado, ModelSim) only. Complete all work up to running on the VCU118. Long torino runs are launched by me over SSH, after asking.

| Stage | Work |
|---|---|
| P2-2 | SAT-Accel to RTL with Vitis HLS only |
| P2-3 | Replace `v++ -l` (block design: k2k.cfg streams, AXI interconnect) and XRT (driver core); standalone RTL testbench with real images; out-of-context netlist for ESP |
| P2-4 | ESP third-party tile for SAT-Accel (wrapper, xml, `thirdparty.py`), Linux KMD + UMD, bare-metal test, VCU118 SoC (Ariane + NVDLA + SAT-Accel), full-SoC simulation |
| P2-5 | Ariane software: NVDLA inference, then the serial and double-buffered baselines on real hardware, `rdcycle` timing |
| P2-6 | ReasonLink RTL in the SAT-Accel tile (separate tiles: it reads NVDLA's output buffer from DRAM), mechanisms switchable for ablations |
| P2-7 | VCU118 bitstream, Linux image, board run and analysis scripts |

### P2-2: SAT-Accel to RTL with Vitis HLS only (`sataccel_hw/`)
```
# on torino (Vitis HLS / Vivado 2023.2 on PATH)
cd sataccel_hw
make hls        # build/src = SAT-Accel kernels + patches/; 7 kernels in parallel, ~5 min
make regs       # HLS register headers -> drv/hls_regs (committed)
```
- **Flow.** `vitis_hls -flow_target vitis` per kernel, with SAT-Accel's own kernel/source grouping (`runCompile.sh`) and `-DFPGA_HW`: the same compile `v++ -c` does, exported as a Vivado IP instead of an `.xo`. Part xcvu9p-flga2104-2L-e (VCU118), 10 ns (ESP's VCU118 SoC runs at 78 MHz). Interfaces match what XRT drives: `ap_ctrl_chain` on AXI-Lite, AXI-Stream per `k2k.cfg`, m_axi masters.
- **Vendor include path.** `ap_utils.h` is under `$XILINX_HLS/include/etc` in 2023.2.
- **Capacity patch (`patches/0001-capacity-vcu118.patch`).** At default capacity HLS estimates 794/960 URAM (83%), too much next to Ariane + NVDLA. The patch halves `_FPGA_MAX_CLAUSES` (65,536) and `_FPGA_MAX_LITERAL_ELEMENTS` (512K). `_FPGA_MAX_LITERALS` stays 32,768, see below.
- **Finding: pqHandler reads past its heap.** `swapLower` reads children at `2*pos+1`/`2*pos+2` before bounds-checking them, so the queue must hold `2*num_vars+2` entries: instances need `num_vars <= _FPGA_MAX_LITERALS/2`. Halving `_FPGA_MAX_LITERALS` made `bmc-ibm-5` (9,396 vars) crash in the C model. Our driver refuses instances above `max_vars/2`.
- **Functional check with the patch** (`results/p2/vcu118_caps/`): 16/16 benchmarks and 1000/1000 Sudoku, decision/conflict counts identical to the unpatched runs.

HLS estimates (`results/p2/hw/hls_summary_vcu118.md`; Vivado synthesis is authoritative):

| | LUT | FF | BRAM_18K | URAM | DSP |
|---|---|---|---|---|---|
| 7 kernels, patched | 284,972 (24%) | 127,696 (5%) | 338 (8%) | 450 (47%) | 44 (1%) |

All kernels meet 10 ns (worst estimate 7.73 ns, solver).

### P2-3: replacing v++ and XRT
```
make bd           # Vivado block design (build/vivado_bd), ~3 min
make sim-setup    # xsim compile + elaborate, ~2 min
make tb_tool
bash tools/run_rtl_tests.sh build/rtl_smoke sudoku:0      # ~5.5 min per puzzle
```
- **Block design (`vivado/sataccel_bd.tcl`), in place of `v++ -l --config k2k.cfg`.** The 7 kernel IPs, the 12 `stream_connect` lines, `messageStream` through a depth-16 FIFO (`:16`), a SmartConnect merging the 7 m_axi masters into one 64-bit AXI4 master (32-bit addresses, no IDs), and a SmartConnect fanning one AXI-Lite slave out to the kernels' control ports at 64 KB windows.
- **Shell RTL (`rtl/`).** `sataccel_top` = `sataccel_shell` (APB decode; `apb2axil` APB→AXI-Lite bridge; counters: cycles, busy = solver start to ap_done interrupt, memory active/stall cycles, read/write beats) + the block design. Interface = ESP's third-party socket: 32-bit APB, one 64-bit AXI4 master, one interrupt.
- **Driver core (`drv/sataccel_drv.c`), in place of XRT.** Buffer layout, `setArg` (host.cpp 476-541), the 7 starts in enqueue order, wait with `ap_continue` (ap_ctrl_chain), read-back. Register offsets come from the HLS headers. Register access is a backend: a record backend for the testbench now; MMIO backends for Linux and bare metal in P2-4.
- **Testbench.** `tools/tb_tool` builds the image (hardware capacities, `drv/sataccel_caps.h`) and runs the driver core with the record backend (ops + memory files); `tb/tb_sataccel.sv` replays the ops over APB against `tb/axi_mem.sv` (sparse memory, 30-cycle read latency, one burst at a time); `tb_tool check` decodes the dump through the same driver code.

**RTL smoke test (`results/p2/hw/rtl_smoke.txt`).** Sudoku puzzle 0 **passes** in RTL: SAT, every clause satisfied, grid equal to the reference; 1 iteration, 1 decision, 0 conflicts, identical to the functional model. 203,620 cycles from counter clear to done (busy 202,894 = 2.03 ms at 100 MHz); memory stalled in 187,002 cycles under this memory model; 81,476 read beats (≈ the image's used bytes). xsim runs about 620 cycles/s.

**RTL batch (`results/p2/hw/rtl_batch.txt`): all 16 pass** (Sudoku 1-10, quinn, aalto, aalto_2, aim50, nqueens_8, dubois20; SAT and UNSAT both).

| Test | Result | Decisions / conflicts / restarts | Busy cycles (100 MHz) |
|---|---|---|---|
| Sudoku 0-10 (10 SAT, 1 UNSAT) | all correct, grids = reference | 1/0/0, except puzzle 3: 3/1/0 | 188,484 (UNSAT) to 207,411; typical 202,900 (2.03 ms) |
| quinn, aalto, aalto_2, aim50, nqueens_8 | correct | identical to the functional model | 2,392 to 33,051 |
| dubois20 (UNSAT) | correct | 352/175/1 (functional model: 370/177) | 165,704 |

- **Search traces.** Identical to the functional model on 15/16 tests. dubois20, the only test that restarts, takes a slightly different path. Likely cause (not yet verified): the RTL is built with `-DFPGA_HW`, and SAT-Accel's sources have separate code for that path (restart, discover, learn, clause store), while the functional model runs the sw_emu path.
- **Where Sudoku's time goes.** About 81,450 read beats (650 KB) per puzzle, the copy-in of the image, and the memory master is stalled in 92% of cycles under this testbench's memory model (one burst at a time, 30-cycle latency). The search itself is short. The board will measure the real memory behaviour.
- **Narrow writes.** Each run issues one narrow write (awsize < 3). ESP's AXI-to-NoC proxy (`rtl/sockets/proxy/axislv2noc.vhd`) handles byte/half-word/word/double-word sizes, so this is supported.

**Out-of-context synthesis (`make synth`, 17 min; `results/p2/hw/synth_ooc.md`).** The whole subsystem (kernels, floating-point cores, SmartConnects, shell) synthesized for xcvu9p and written as a structural netlist, `build/vivado_bd/sataccel_top_funcsim.v` (121 MB, not committed; regenerate with `make hls bd synth`), which is what ESP's third-party flow will consume in P2-4.

| | LUT | FF | BRAM36 | URAM | DSP |
|---|---|---|---|---|---|
| sataccel_top | 111,912 (9.5%) | 118,666 (5.0%) | 180.5 (8.4%) | 434 (45.2%) | 44 (0.6%) |

- Per kernel (`results/p2/hw/synth_ooc_utilization_hier.rpt`): solver 68.3k LUT, memory SmartConnect 14.8k, clause_store_handler 11.2k, pqHandler 9.2k; the shell is 246 LUTs.
- HLS overestimated LUTs by 2.5x (285k estimated).
- **Timing.** WNS +2.02 ns at 10 ns post-synthesis (`vivado/timing_from_dcp.tcl`; the first run had no clock applied because the constraint file was scoped to out_of_context, now fixed). Critical path: pqHandler's double-precision adder (VSIDS decay). ESP runs the SoC at 78 MHz (12.8 ns).

### P2-4: SAT-Accel in an ESP SoC (done 2026-11-05)
Everything ESP needs is in `esp_soc/`, linked into an ESP checkout by `install.sh` (ESP stays a clean checkout apart from two lines in `tools/socgen/thirdparty.py`).

```
# on torino, once: ESP submodules (Ariane, esp-caches, NVDLA ip, Linux, OpenSBI, riscv-pk, riscv-tests)
cd ~/esp && git submodule update --init --recursive rtl/cores/ariane/ariane soft/ariane/riscv-tests
git submodule update --init rtl/caches/esp-caches accelerators/third-party/NV_NVDLA/ip soft/ariane/opensbi soft/ariane/riscv-pk
git submodule update --init --depth 1 soft/ariane/linux
cpanm -l ~/perl5 Capture::Tiny YAML IO::Tee Clone       # NVDLA tmake (export PERL5LIB=~/perl5/lib/perl5)
bash ~/ReasonLink/esp_soc/buildroot/build_rootfs.sh     # Linux root file system (~30 min)
bash ~/ReasonLink/esp_soc/install.sh

cd ~/ReasonLink/sataccel_hw && make hls bd synth         # SAT-Accel netlist (Stages 2-3)
cd ~/esp/socs/reasonlink-vcu118
make esp-config                                          # 2x3 SoC from esp_soc/socs/reasonlink-vcu118/esp_defconfig
make sataccel-baremetal                                  # soft-build/ariane/baremetal/sataccel.exe
(cd ~/esp/accelerators/third-party/NV_NVDLA && make TECH_TYPE=fpga hw)   # NVDLA RTL
make -j8 linux && make CU_SATACCEL NV_NVDLA && make -j8 linux           # kernel, then drivers/apps, then image
TEST_PROGRAM=$PWD/soft-build/ariane/baremetal/sataccel.exe make sim     # full-SoC simulation (vsim.tcl: 60 ms)
```

- **SoC (`esp_soc/socs/reasonlink-vcu118/esp_defconfig`).** 2x3 NoC: mem, Ariane (ESP caches on), NVDLA (tile 2), IO, SAT-Accel (tile 4), empty. Device tree: `columbia,sataccel` at `0x60500000` (1 MB window), PLIC interrupt 7, ESP's reserved third-party pool (`0xA0200000`-`0xBFFFFFFF` here).
- **Tile (`esp_soc/accelerator/CU_SATACCEL`).** XML (device ID 0x101, vendor `columbia`), `CU_SATACCEL_wrapper.v` (ESP's third-party port names onto `sataccel_top`; IDs, atop, region driven 0), and the out-of-context netlist from P2-3 with its `glbl` module removed (ESP's simulation compiles `glbl.v` itself). The socket hands over `paddr & 0x0FFFFFFF` and a 1 MB-aligned window, so the shell's `paddr[19:0]` decode applies unchanged; the interrupt is level-sensitive.
- **Coherence.** ESP's third-party socket supports non-coherent, LLC-coherent and coherent DMA, set in the tile's CSR (word 99 at `0x60090000 + tile*0x200`, as in ESP's `nv_nvdla_coh`). We use coherent DMA (`ACC_COH_RECALL`): no cache flushes around a run.
- **Linux driver (`sw/kmd/sataccel.c`, `/dev/sataccel`).** Platform driver for `columbia,sataccel`; 32 MB DMA buffer from the device-tree memory region; mmap of registers and buffer; completion interrupt (clears the solver's ap_done status) with `SATACCEL_IOC_WAIT`; sets the coherence CSR at probe (`tile` module parameter, default 4). Built by ESP against its 5.1 kernel.
- **User space (`sw/umd`).** `libsataccel` = the P2-3 driver core over the mmap'ed registers, waiting on the interrupt; `sataccel_run` (static) solves CNF files or Sudoku puzzles, checks the answers, and prints CPU cycles for image build, load and solve plus the tile's busy/memory counters.
- **Bare-metal test (`esp_soc/apps/baremetal/sataccel`, `make sataccel-baremetal`).** Replays the driver core's recorded register program for one instance (`sataccel_test.h`, generated by `sataccel_hw/tools/gen_baremetal_header.py`; now `quinn`) after copying its image, then checks the result. For full-SoC simulation and first bring-up on the board.
- **Garbage-fill check (`results/p2/hw/rtl_fill.txt`).** quinn, Sudoku 0 and dubois20 pass in RTL with only the image's used ranges loaded and every other word reading 0xdeadbeef, with search and cycle counts identical to zeroed memory: SAT-Accel reads nothing outside the image, so lean loads need no zeroing.
- **Linux image (`soft-build/ariane/linux.bin`).** OpenSBI + Linux 5.1 with an initramfs holding BusyBox, `sataccel.ko`, `sataccel_run`, `opendla.ko`, `nvdla_runtime` and ESP's demo LeNet loadable.

ESP issues found and worked around:
- **Tile lines in `.esp_config`.** ESP's `soc.py` splits tile lines on single spaces, in the writer's order `TILE_y_x = i acc NAME point has_l2 has_tdvfs vendor`. A third-party accelerator has an *empty* design point, written as a double space: `acc NV_NVDLA  0 0 nvidia`. ESP's own NVDLA example config (`0 0 0  0 nvidia`) has one token too many, which drops the vendor (device-tree compatible `",nv_small"`, so NVDLA's driver would not bind). A `0` design point instead makes socmap reference `HLSCFG_<NAME>_0`, which only ESP-native accelerators define, and the RTL compile fails (seen in our first full-SoC simulation).
- **Root file system.** `soft/ariane/sysroot` held only ESP's 60 KB overlay; the buildroot step of ESP's toolchain script had never run. `esp_soc/buildroot/build_rootfs.sh` runs it non-interactively, with buildroot 2020.02.12 instead of ESP's 2018 pin (which knows GCC <= 8 and headers <= 4.18; the installed toolchain is GCC 9.2 with 5.0 headers) and SHA-256 passwords (MD5 was removed from buildroot).
- **NVDLA RTL generation** needs Perl's Capture::Tiny (and YAML, IO::Tee, Clone), installed locally with cpanm.
- **riscv-tests** must be initialized for any bare-metal build (`util.h`, crt).
- **`make sim` without `vsim.tcl`** leaves `vsim -c` at a prompt; `esp_soc/socs/reasonlink-vcu118/vsim.tcl` runs a bounded time and quits.
- **Third-party tiles need an empty design point** in `.esp_config` (see the tile-line note above); the first full-SoC simulation failed to compile on `HLSCFG_*_0`.

**Full-SoC simulation (ModelSim, bare-metal `quinn`), status 2026-10-23 13:00:** Xilinx simulation libraries compiled once (48 min). The simulation started 00:50; the UART transcript shows Ariane booting the test, ESP's plug-and-play finding the tile (`apbctrl: slv297: Columbia University SLD SAT-Accel CDCL solver (HLS)`, interrupt 7), the shell ID `0x5a7a0001` read over APB through the NoC, coherent DMA set, and the 602-word image loaded (by 10:15). The register replay and the solve have not reported yet (no output since). It is either very slow (Ariane + the 121 MB gate-level SAT-Accel netlist) or stuck; ModelSim has no progress indicator in batch mode. It is bounded at 60 ms of simulated time. If it does not report, the next step is a waveform/transcript check of the first APB writes after the image load.

**Diagnosis (2026-10-23/30): the simulation was hung, not slow.** Ariane's instruction trace (`modelsim/trace_hart_0.log`) stops at 03:33 on op 40 of the register program: every kernel start has been written, and the next op polls kernel 6's `ap_done` (an APB read of `0x60560000`). Before that, the simulation advanced about 1.2 µs of simulated time per second, so thousands of polls would have shown up; the CPU was stalled on the read. Cause: ESP's `noc2apb` (`rtl/sockets/proxy/noc2apb.vhd`, state `rcv_address`) drives a **read** with `psel` and `penable` together from the first cycle, with no setup phase (writes do get one). `apb2axil` started a transfer only on `psel && !penable`, so a read never became an AXI-Lite read and `pready` never came. The shell's own registers answer combinationally, which is why the ID read worked; the standalone testbench drives textbook APB, so the RTL tests never saw it. The run then ran to its 60 ms bound (51 h, ended 2026-10-25) without a result.
- **Fix:** `apb2axil` starts a transfer on `psel` (setup phase or ESP's read).
- **Tests:** `tb/tb_apb2axil.sv` (std and ESP-timed reads, back to back): the old bridge hangs on the first ESP-timed read, the fixed one passes. `tb_sataccel.sv` now drives reads with ESP's timing by default (`APB=std` for textbook APB), so the RTL regression covers it.
- Both OOC netlists (the shell is inside them) must be rebuilt; the netlists before 2026-10-30 hang on the first kernel-register read on the board too.
- **Rebuilt and retested 2026-10-30** (`~/rl_rebuild.sh` on torino): RTL **10/10 PASS** with ESP-timed reads: baseline build Sudoku 0 and 5, quinn, dubois20, plus Sudoku 0 and quinn with textbook APB; ReasonLink build rl:0, rl:3, rl:5 and Sudoku 0. Cycle counts are unchanged (Sudoku 0 busy 202,894). Both OOC netlists rebuilt and copied into the tile.

### P2-4 addendum: bitstream flow
- **Encrypted IP.** Vivado refuses the funcsim netlist as a synthesis source (it holds encrypted Xilinx IP, written for simulation only). The tile's single Verilog entry `sataccel_top_sel.v` is a black-box stub under `` `SYNTHESIS `` and the funcsim netlist otherwise (ModelSim unchanged). The SoC's `make vivado-syn-rl` (added by `install.sh`) creates ESP's Vivado project, adds `sataccel_top.dcp` to fill the stub (`add_sataccel_dcp.tcl`), then runs ESP's synthesis on that project, answering "n" to ESP's "overwrite existing Vivado project?" prompt (appending to `srcs.tcl` does not work: ESP regenerates it).
- **Baseline bitstream, first attempt** (`make vivado-syn-rl`, 2026-10-23): synthesis passes (10:49-12:13), **implementation fails** at `opt_design`'s DRC: `INBB-3 Black Box Instances: ... CU_SATACCEL_rtl_i/acc of type 'CU_SATACCEL_sataccel_top' has undefined contents`. ESP reads third-party sources into a library named after the accelerator, so Vivado names the stub's cell type `CU_SATACCEL_sataccel_top`, while the checkpoint's top is `sataccel_top`: the checkpoint is never linked. Candidate fixes: (a) link it explicitly in implementation (`read_checkpoint -cell <that cell> sataccel_top.dcp` as an `opt_design` pre-hook, `STEPS.OPT_DESIGN.TCL.PRE`); (b) write the out-of-context checkpoint with its top renamed to `CU_SATACCEL_sataccel_top`. The other critical warnings (unclocked `FDCP` in ESP's mem tile) come from ESP itself.
- **Fix (a), applied: `esp_soc/socs/reasonlink-vcu118/sataccel_impl.tcl`** (replaces `add_sataccel_dcp.tcl`). Each implementation run gets an `opt_design` pre-hook that runs `read_checkpoint -cell` on the black box. Synthesis does not depend on which SAT-Accel is linked, so one synthesized SoC serves two runs: `impl_1` (baseline, `top.bit`) and `impl_rl` (SAT-Accel + ReasonLink from `CU_SATACCEL/out/sataccel_top_rl.dcp`, `top_rl.bit`). Targets: `make vivado-syn-rl` (new project, ESP's synthesis + `impl_1`), `make vivado-impl-rl IMPL_RUNS="impl_1 impl_rl"` (implementation only, runs in parallel). `CU_SATACCEL/Makefile`: `hw-rl` copies the ReasonLink checkpoint; `sataccel_top_sel.v` is rewritten only when its contents change, so a new netlist does not invalidate ESP's synthesis.
- **Verified 2026-10-23:** a standalone check (open the SoC's synthesized checkpoint, `read_checkpoint -cell`) leaves only ESP's own MIG and SGMII IP black boxes. In `impl_1` the hook links the checkpoint, the clock at the SAT-Accel pin is the SoC's `mmcm_clkout1` (the OOC `create_clock` is not applied), and `opt_design`'s DRC finishes with 0 errors.
- **Next failure: `place_design` DRC `PORTPROP-6`** on the SGMII pins (`rxp` has ODT, `txp` has OUTPUT_IMPEDANCE, both LVDS). ESP's `xilinx-vcu118-xcvu9p-eth-pins.xdc` clears them with `set_property ODT {}`, which Vivado 2023.2 still counts as set, so stock ESP would fail the same way. The hook now also runs `reset_property` on them.
- **Stub date:** Vivado's synthesis stub carries a `// Date` comment, so every new netlist rewrote `sataccel_top_sel.v` and flagged ESP's synthesis out of date (caught by `sataccel_impl.tcl`'s check). The tile Makefile now drops the stub's comment lines. For the 2026-10-30 rebuild, where only that comment changed (ports come from `rtl/sataccel_top.v`, unchanged since 2026-10-22), `SATACCEL_SYNTH_CURRENT=1 make vivado-impl-rl IMPL_RUNS="impl_1 impl_rl"` marks synth_1 up to date (Vivado's force-up-to-date) instead of a new 84 min synthesis.
- torino is shared: never kill processes by name pattern (another user runs ESP builds too).
- **Correction (2026-10-31): the OOC clock *is* applied.** With the rebuilt netlists, `read_checkpoint -cell` applies the checkpoint's `create_clock -name clk -period 10` to the net feeding the SAT-Accel cell, in place of the 12.8 ns tile clock `mmcm_clkout1` (the 2026-10-23 check above saw `mmcm_clkout1` with the older netlist). ESP's runs routed both designs, but timed every SAT-Accel/tile <-> SoC path as a crossing between unrelated clocks: WNS -4.1 / -4.4 ns, about 7-8k failing endpoints. Both bitstream writes were then killed by memory (two Vivado writes plus the SoC simulation). Vivado 2023.2 cannot delete a clock or skip a checkpoint's constraints (`read_xdc -mode out_of_context` does not keep it out either; checked on a toy design), so `sataccel_clock.tcl` replaces it with a generated copy of the tile clock on the same pin and adds it to ESP's asynchronous groups; the link hooks call it. A second gap is ESP's: its `clkm`/`clkm1` (two DDR controllers) asynchronous group is never applied because no net `clkm1` survives synthesis; `sataccel_fix_ddr_groups` declares it from the MIGs' MMCMs (impl_rl had failed by 0.555 ns on 3 DDR0 -> DDR1 reset endpoints). A pre-bitstream hook (`sataccel_check_timing`) stops unless setup slack >= -`SATACCEL_MAX_VIOLATION` and hold is met.
- **Bitstreams (2026-10-31, `results/p2/m2/bitstreams.md`):** the routed designs were kept; `make vivado-bit-rl` (`sataccel_bit.tcl`) reopens each routed checkpoint as the run does (MIG calibration ELF loaded), applies the fixes, checks timing (allowed -0.1 ns) and writes the bitstream. `top.bit` (impl_1): WNS -0.059 ns on 4 endpoints in Ariane's FPU (ESP's CPU tile), SAT-Accel +2.546 ns, crossings +7.2/+10.4 ns, 341k LUT. `top_rl.bit` (impl_rl): WNS +0.036 ns, WHS +0.009 ns, SAT-Accel +2.681 ns, 345k LUT. URAM 434 (45%) in both.
- **Full-SoC failure explained: ESP ignores AXI write strobes (2026-11-03, `results/p2/m2/wstrb_fix.md`).** A second full-SoC simulation dumped the tile's AXI port (`SATACCEL_AXI_VCD`), checked by `sataccel_hw/tools/check_axi_vcd.py`: all 528 read beats match the image, and SAT-Accel writes the right result (SAT, 5 decisions, 1 conflict). ESP's `axislv2noc` writes the bytes `AWSIZE` selects and never reads `WSTRB`; SAT-Accel's HLS ports write 32-bit values as 64-bit beats with one half enabled and pad bursts with empty beats (238 of 251 beats), so its results are overwritten; with the strobes ignored, the trace gives exactly what the CPU read. Fix: `esp_soc/accelerator/CU_SATACCEL/axi_wstrb_split.v` in the tile wrapper replays each beat as aligned single-beat writes of its enabled bytes (empty beats dropped, B after the last downstream B). RTL with an ESP-like memory (`axi_mem +esp_wr`, `run_rtl_tests.sh` `ESP_WR=1 SPLIT=1`): without the adapter quinn fails as in the SoC; with it 7/7 pass (quinn, aalto, aalto_2, aim50, dubois20, nqueens_8, Sudoku 1) with identical searches, cycle cost +0.2% to +17% (Sudoku +3.1%). The planned non-coherent repeat did not run: the new bare-metal program had a compile error and ESP's `make sataccel-baremetal` silently copied the previous binary (now fixed; check the program's banner in the transcript).

### P2-5: board baselines (`board/`)
```
python workload/export_board.py          # data/board: each MNIST image once + emulator logits (7.8 MB)
bash workload/nvdla_compile.sh           # also build/nvdla/rl_lenet_ip2.nvdla (torino)
make -C board host                       # x86 test build over a fake /dev/sataccel
# ESP: make NV_NVDLA CU_SATACCEL && make linux  -> /root/CU_SATACCEL/rl in the initramfs
# board: sh /root/CU_SATACCEL/rl/run_baselines.sh ; then python3 board/summarize_board.py <results>
```
- **`rl_board`.** P1's serial and double-buffered handoff with the real accelerators: NVDLA classifies every given cell (`rl_nvdla.cpp`, a C wrapper over NVDLA's runtime filling the input tensor as `nvdla_runtime` does), then argmax, image build, transfer into the SAT-Accel tile's DMA buffers, solve. `rdcycle` per phase; T_sat = the tile's busy counter. Double buffering uses two SAT-Accel buffer sets (driver DMA buffer 64 MB). Every answer checked against PySAT, every NVDLA output against the int8 emulator (bit-exact expected).
- **Logits loadable.** `rl_lenet_ip2.nvdla` is our classifier without the Softmax layer (NVDLA outputs the int8 ip2 logits, the handoff's input per the 2026-10-16 decision; quantized softmax can tie where logits do not). 444,080 B, md5 `de91e998...`.
- **Handoff modes:** `full` (P1's), `delta` (ReasonLink iii as a CPU-only optimization: base image resident, givens as fixed decisions; the proposal's ablation), `reasonlink` (the tile's ReasonLink core, phase B).
- **Host test** (fake device running sataccel_sim): serial/double x faithful/lean/delta all match the reference; delta writes 3,940 B per puzzle (lean 632 KB, faithful 5.3 MB).
- **`summarize_board.py`:** correctness checks plus the proposal's decomposition (T_nn, T_conv, T_xfer, T_sync, T_sat; serial handoff share; exposed handoff for double-buffered runs).

**Board run (2026-11-05, `top_rl.bit`, `linux.bin` bd35ffcb, 100 puzzles; CSVs and logs in `results/p2/board/`).** Bring-up checks before every batch: base.cnf SAT in 947k-954k cycles with its interrupt, NVDLA's seven.pgm output = ESP's published one.

| Configuration (100 puzzles): wall time, measured / without SSH stalls (below) | Serial | Double-buffered |
|---|---|---|
| faithful (full image, clear all) | 397.8 / 387.8 s | 394.4 / 386.7 s |
| lean (used ranges) | 303.7 / 299.4 s | 296.2 / 295.7 s (first attempt: NVDLA hung at about puzzle 75) |
| delta (givens as fixed decisions, CPU) | 259.2 / 250.7 s | 247.1 / 247.0 s |
| delta + resident | 249.4 / 249.4 s | 246.8 / 246.8 s |
| ReasonLink, before the layout fix | 255.5 s, wrong | - |
| ReasonLink + resident, before the fix | 253.1 s, wrong | - |
| **ReasonLink (layout fix)** | **282.1 / 250.2 s** | **247.2 / 247.2 s** |
| **ReasonLink + resident (layout fix)** | **270.8 / 249.5 s** (first attempt: NVDLA hung at about puzzle 15) | **246.7 / 246.7 s** |
| faithful, NVDLA replaced by its logits (trace) | 140.4 / 140.4 s | - |

- **SSH stalls (measurement artifact).** An SSH login to the board costs its 78 MHz CPU about 1-1.5 s, which lands in whatever `rl_board` is doing (almost always an NVDLA classification). My monitoring polled the board during the runs (every 5 min for the fetch loop, every 20-90 s for status checks), so some puzzles took 0.3-3 s longer; the ReasonLink reruns, polled every ~40 s, had the most (19 and 14 puzzles). With no SSH during a 20-puzzle ReasonLink run: 0 stalls, 79.1-79.4 ms per cell. The corrected column removes, per puzzle, the NVDLA time above the run's median per cell (79.2-79.5 ms in every configuration). Rule from now on: no SSH to the board while a measured run is going.

- **Correctness (`summarize_board.py`, `results/p2/board/csv/board_summary.md`).** Every configuration except the two buggy ReasonLink runs passes: 98/100 equal PySAT's grid and the other 2 are valid alternative solutions (valid Sudoku keeping every predicted given), so 100/100 are correct (81 SAT, 19 UNSAT from misclassified givens); grids, SAT/UNSAT and decision counts are identical across configurations. The summary now passes a run on its answers and reports NVDLA's logit differences separately (they were expected to be zero).
- **Double-buffered exposed handoff** (`summarize_board.py`): faithful 34.5% of wall time (serial share 34.9%), lean 16.0% (16.3%); delta and ReasonLink, with and without the resident base, slightly below zero: the solve itself is hidden behind the next puzzle's NVDLA work, and nothing of the handoff is exposed. The last double-buffered batch (5 configurations, 500 puzzles, no SSH during the runs, no hang) also had NVDLA about 1% faster per cell (78.5-78.7 vs 79.2-79.4 ms; not explained), which is most of its gain over the serial runs (delta 247.0 vs 250.7 s).
- **NVDLA vs the int8 emulator.** 925 of 3,141 given cells have logits that differ from the emulator, but no digit differs: the NVDLA and trace runs give identical grids on 100/100 puzzles.
- **Where the time goes (median ms per puzzle from the CSVs' `rdcycle` counts, 78.1 MHz; NVDLA 79.3 ms per cell x 31.4 cells, about 2,490 ms, in every configuration):**

| Serial | argmax | image build | transfer (bytes) | T_sat | handoff share of e2e (corrected) |
|---|---|---|---|---|---|
| faithful | 0.14 | 722 | 609 (5.29 MB) | 11.0 | 34% |
| lean | 0.14 | 412 | 73 (632 KB) | 10.6 | 16% |
| delta | 0.14 | 0 | 0.34 (3.9 KB) | 10.2 | 0.02% |
| delta + resident | 0.14 | 0 | 0.34 (3.9 KB) | **3.1** | 0.02% |
| ReasonLink | - | 0 | 0.06 commit (2.9 KB) + 0.6 push/wait | 10.1 | 0.03% |
| ReasonLink + resident | - | 0 | 0.06 + 0.6 | **3.0** | 0.03% |

  H1 on the board: in the faithful baseline the handoff (image build + transfer) is 34% of the run and 121x the SAT-Accel solve, dominated by CPU image building and full-image loading, not by NVDLA's 310 B of logits. The delta removes it; the resident base then cuts SAT-Accel's solve by 70% (10.2 to 3.1 ms) and its DRAM reads per solve by 74% (651 KB to 166 KB, median `tile_rd_beats`; the tile is memory-stalled 98% of its busy time without the resident base, 90% with it), as in RTL. What is left is NVDLA under Linux, about 99% of a delta run.
- **Double buffering barely helps the faithful run** (386.7 vs 387.8 s corrected). It can hide at most T_sat (about 11-13 ms per puzzle) behind the next puzzle's preparation, and the preparation itself (NVDLA, image build, transfer) is all CPU-side and serial: H2's case where the handoff is longer than the slower stage. The double-buffered CSV's `sync` (3.9 s per puzzle) is the time from start to completion detection, which now includes the next puzzle's preparation.
- **ReasonLink after the fix (serial):** grids, SAT/UNSAT, decisions and conflicts identical to the faithful run on 100/100 (with the resident base too: T_sat 3.0 ms, the same 21,350 read beats as delta + resident). Per puzzle (`RL_TIMING`, 10 puzzles): the tile converts a cell in about 85 tile cycles (CYC_CONV) and writes the 2,948 B delta in 0.06 ms; the CPU's push and credit wait cost about 18 us per cell (two MMIO writes and status polls over ESP's APB), 0.6 ms per puzzle. So ReasonLink's exposed handoff (about 0.7 ms) is the same order as the CPU delta's (0.5 ms: argmax + 3.9 KB write), and both vanish next to NVDLA's 2.5 s: end to end they tie (250.2 vs 250.7 s; 249.5 vs 249.4 s with the resident base). The "9% slower" in the measured wall times was the SSH stalls above. Waiting for each literal instead of the dequeue (`RL_WAIT_LIT`) changes nothing.
- **ReasonLink bug** (bug 4 in `results/p2/m2/board_bringup.md`): NVDLA's 1x1x10 output has an 8 B surface stride, which ReasonLink's LAYOUT register (units of 256 B) could not hold; the driver rounded it to 0 and ReasonLink read channels 8, 9 from channels 0, 1. Every solve ended UNSAT (the 19 "matches" were the 19 UNSAT puzzles). Fixed in `sa_rl_setup`: with an 8 B stride the surfaces are adjacent, so it programs the contiguous layout the RTL test already used; other unrepresentable strides are now an error. `rl_board` gained `RL_DEBUG` (per-cell logits, raw buffer bytes, the tile's literals against the CPU's).
- **Board operation (scripts in `board/ops/`, installed in `~/m2/` on torino).** A hung accelerator survives `esplink --reset` (the cores reset, NVDLA stays wedged): reprogram the FPGA after a hang (`boot_and_run.sh`). EDCL loads fail often and are retried (`boot_retry.sh`). Results come off the board as text every 5 min (`board_fetch.sh`); there is no tar on the board.

### P2-6: ReasonLink
**Two measurements that shaped the design.**
- **Image delta (`results/p2/image_delta.txt`):** a puzzle's SAT-Accel image differs from the base formula's by 1,212 B of 632,313 B (0.19%) on average.
- **Fixed decisions (`results/p2/delta_check.txt`, `sataccel_sim/delta_check.cc`):** SAT-Accel's host conversion already turns unit clauses into level-0 fixed decisions (answerStack prefix + miscCounters[3]). Passing the givens only that way, on one base image restored between puzzles, gives SAT/UNSAT 1000/1000 and the same grids (same 9 valid alternatives) as the unit-clause images. The kernels write back only answerStack, miscCounters, trackLBD and the device-only buckets, so the base image stays valid in DRAM.

**Phase B (`sataccel_hw/rtl/reasonlink_core.v`, block design `make bd REASONLINK=1`):** in the SAT-Accel tile (separate tiles). Per given cell the CPU pushes {cell, address of NVDLA's output}; the core fetches the 10 int8 logits, takes the argmax over digits 1-9 (ties: lowest), queues the literal on chip and, on COMMIT, writes the delta (answerStack prefix + zeros, base miscCounters with the count). Mechanisms: (i) hardware literals, (ii) on-chip FIFO with a credit wait per cell (NVDLA reuses one output buffer), (iii) delta transfer at the DRAM level, (iv) conversion overlapped with NVDLA.
- Unit test `tb/tb_reasonlink.sv`: PASS (ties, negative logits, both NVDLA layouts, odd num_vars, second puzzle).
- End to end in RTL (`results/p2/hw/rtl_reasonlink.txt`): puzzles 0, 3, 5 PASS with search counts identical to the unit-clause runs; conversion about 67 cycles per cell, delta write about 1,200 cycles; the unmodified flow still passes in the same build.
- Board path: `rl_board --handoff reasonlink`; `sataccel.ko` translates NVDLA's output buffer address (VA->PA ioctl, PFN mappings included) and puts NVDLA's tile in coherent-DMA mode too, so the core reads NVDLA's output coherently.
- What phase B leaves: SAT-Accel still copies the whole base image in from DRAM each solve (about 150k of 203k cycles in RTL). Phase A (resident on-chip base, rollback of assignments and learned clauses) targets that.

Naming: the P1 report calls phase B **ReasonLink Milestone 1** and phase A **Milestone 2**; both names are used below.

**Milestone 2 / phase A: the base formula resident on chip (`sataccel_hw/patches/0002-resident-base.patch`).** This is the proposal's mechanism (iii) in full ("the base formula stays resident in SAT-Accel ... learned clauses are rolled back to the base formula between instances"); Milestone 1 did its DRAM half. It needs no new RTL: in hardware the kernels' on-chip arrays already keep their contents between calls; what the patch adds is a copy-in that only restores what a solve changes.

*Where the state lives and what a solve changes* (from the kernel sources):

| Kernel | On-chip state copied in | Changed by a solve? | Resident copy-in |
|---|---|---|---|
| clause_store_handler | clause store 203 KB, clause metadata 96 KB | **No** for base clauses: learned clauses get IDs from `ORIGINAL_CLS_CNT` and pages from `clauseElements` on (both allocators are rebuilt from those scalars each call); deletion takes IDs only from the learned-clause buckets | skipped |
| solver | literal store 187 KB | Only after each list's base entries: learned clauses go into the free tail of a literal's last base page and into new pages linked from its last word; deletion moves learned entries only | skipped; tails cleared (below) |
| solver | literal metadata 46 KB, clause states 96 KB | Yes (assignments, levels, phases, list ends, counters) | copied from the DRAM base image as before |
| solver | answer stack 3 KB | Yes; holds the givens | copied (Milestone 1's delta) |
| location_handler, pqHandler, restart, timer | learned-clause positions; VSIDS heap | Rebuilt or overwritten each call | unchanged |

*The patch.* `residentBase` (new `s_axilite` scalar) on the solver and the clause store handler. With it set, `copy_litStore` and `copy_cls_data` get a trip count of 0 (the dataflow structure is unchanged), and after the metadata is copied in, `clean_litStore_tails` zeros every literal's last base page from its first free word to its end (zeros in the image), which restores the base literal store exactly; a variable with free space 0 has no page of its own and is skipped. The three persistent arrays (`mLitStore`, `mClsStore`, `mCmd`) are `static`, which gives the C model the persistence the hardware RAMs already have. Weak `rl_probe_*` hooks (C model only, `#ifndef __SYNTHESIS__`) expose the base regions to the checker. Copy-in per Sudoku puzzle: about 145 KB (clause states, metadata, answer stack) instead of 632 KB, 77% less; the tail clear is one line write per literal side (1,458 for Sudoku).

*Driver.* `sa_dev_start_resident(d, img, resident)` writes `residentBase` when the register map has it (`SA_HAS_RESIDENT`, from the HLS headers); `sa_dev_start` = resident 0. The caller sets resident = 1 only when the previous run used the same base image.

*Functional check (`sataccel_sim/resident_check.cc`, build `make BUILD=build_m2 SATACCEL=../sataccel_hw/build/src build_m2/resident_check`).* Every instance runs twice, fresh (full copy-in) and in a resident sequence (first fresh, the rest with `residentBase` = 1); results, models and all search counters must match, and after every resident copy-in the base regions of the literal store, clause store and clause metadata must equal a fresh copy-in byte for byte.
- **Sudoku** (`results/p2/m2/resident_sudoku.txt`): 1000/1000 identical (808 SAT, 192 UNSAT, 144 conflicts); base regions identical in 999/999 resident runs; before the tails were cleared, the literal store differed in 33 runs (496 B): the clearing is needed.
- **SAT-Accel's 16 benchmarks, 12 fixed-decision sets each** (`results/p2/m2/resident_benchmarks.txt`; none, then 1 to vars/8 random literals): 192/192 identical (53 SAT, 139 UNSAT, 20,405 conflicts, including 16,268 on hole7 with clause deletion); base regions identical in 176/176; tails dirty in 62 runs (39 KB).

*In hardware (2026-10-30/31, `results/p2/m2/rtl_resident.txt`, `results/p2/m2/hls_summary_m2.md`).*
- **HLS:** `static` arrays failed Vitis HLS's dataflow checks (internal globals read by several processes in clause_store_handler). Synthesis keeps them local (`RL_PERSIST` in `data_structures.h`: `static` only in the C model); the generated RAMs keep their contents between calls, which the RTL sequence test confirms. All 7 kernels pass; estimates: solver +12k LUT (the 16-word tail clear), clause_store_handler +167 LUT, URAM unchanged (450), clock estimate unchanged (7.73 ns). Register map: `residentBase` added at solver 0x4c and clause_store_handler 0x6c, all other offsets unchanged (`drv/hls_regs`).
- **RTL, 10/10 PASS** (ESP-timed APB reads). With `residentBase` = 0 the patched kernels behave as before: Sudoku 0 (busy 202,894), quinn, dubois20 (352/175, as before), rl:0. Back-to-back sequences in one simulation (`tb_tool gen-rl-seq`, `run_rtl_tests.sh rlseq:0,3,5:<r>`), counters cleared per puzzle:

| Puzzle | Full copy-in each time (r0): busy / read beats | Base resident (r1): busy / read beats | Change |
|---|---|---|---|
| 0 (first, fresh in both) | 202,558 / 81,404 | 202,558 / 81,404 | - |
| 3 (SAT, 3 decisions, 1 conflict) | 207,383 / 81,386 | **100,553 / 20,798** | -51.5% cycles, -74% reads |
| 5 (UNSAT) | 188,379 / 81,390 | **81,549 / 20,802** | -56.7% cycles, -74% reads |

  Results, grids and search counts are identical in both sequences. What remains of a resident solve is the 20.8k-beat copy-in of the clause states, literal metadata and answer stack (memory stalls are still most of it under the testbench's one-burst memory model) plus the search.
- **OOC netlists** (`make synth`): SAT-Accel 113,062 LUT (+1,150 vs 111,912 before), with ReasonLink 117,698 LUT; URAM 434 (45.2%) and BRAM 180.5 unchanged; WNS +2.019 ns at 10 ns for both.
- **Board:** `rl_board --resident` (with `--handoff delta` or `reasonlink`): every solve after the first uses `sa_dev_start_resident`; valid across both buffer sets of the double-buffered mode, since they hold the same base image and the on-chip stores hold clause IDs and on-chip page indices, not DRAM addresses. The CSV gains `tile_rd_beats` (SAT-Accel's DRAM reads; `summarize_board.py` reports `tile_read_bytes`) and `resident=` in its header; `run_baselines.sh` adds the delta and ReasonLink runs with `--resident`. Host test (fake device over the patched functional model, `--nn trace`, 200 puzzles): delta vs delta + resident identical (SAT/UNSAT, decisions, conflicts, grid) 200/200 in serial and in double-buffered mode; 198/200 equal the reference grid in both (2 valid alternatives).

## Environments

### Local (WSL Ubuntu 24.04 on Windows)
- **Setup:** `bash env/setup_wsl.sh`. This creates `~/venvs/reasonlink` and writes `env/requirements.lock.txt`.
- **Verified 2026-10-16:**
  - Python 3.12.3, numpy 1.26.4, torch 2.4.1+cpu, torchvision 0.19.1+cpu, python-sat 1.9.dev16.
  - The pysat MiniSat22 smoke test passes.
- **Not available locally:** Vitis, Vivado, ModelSim, Xcelium.
- **How commands run:** WSL runs as root, so `~` is `/root`. Run repo commands from `/mnt/c/Columbia_University/Fall2026/AInativeComputing/ReasonLink`.

### Columbia server: `torino.cs.columbia.edu`
- **How we work on it:** the user runs commands over an interactive SSH session and pastes back the output.
- **Credentials:** never commit them. If needed, local settings go in `env/server.env`, which is gitignored.
- **Rule:** no long builds (toolchains, bitstreams, full-SoC simulations) without asking first.
- **Re-running the inventory:** `bash ~/server_probe.sh`. The script is a copy of `env/server_probe.sh`.

**Inventory (2026-10-16, `env/server_probe.sh`):**

| Item | Value |
|---|---|
| OS | RHEL 8.10. ESP's primary platform. |
| glibc | 2.28 |
| CPUs / RAM / home disk | 16 / 62 GB / 903 GB free of 1.3 TB |
| gcc / git / python3 | 8.5.0 / 2.43.7 / 3.8.20. The shell auto-activates the conda env `core-v-mini-mcu`. |
| Vivado | 2023.2. ESP's required version. |
| Vitis HLS | 2023.2 |
| Vitis `v++` / XRT | **not installed**. The unified 2023.2 installer `.bin` is in `/opt`. |
| ModelSim | DE 2023.2. ESP's required version. |
| Xcelium | 23.09 (`/opt/cadence/installs/XCELIUM2309`) |
| RISC-V toolchains | gcc 9.2 for `riscv64-unknown-elf` and `riscv64-unknown-linux-gnu` on PATH; `/opt/riscv` and `/opt/riscv32imc` exist. **We don't need to build ESP's toolchain.** |
| spike / qemu-riscv64 | not on PATH |
| Other | `/opt/Catapult.tar.gz`, `/opt/prodesign` (proFPGA) |
| Licenses | Xilinx, FlexLM and Mentor license servers are set. Hostnames are omitted here. |

**What this means for P1:**
- **ESP:** the full-SoC flow (ModelSim + Vivado + RISC-V) is available as-is.
- **SAT-Accel software emulation:** not available. `runCompile.sh` uses `v++ -t sw_emu`, which needs XRT, and installing XRT needs root.
- **Vitis HLS C simulation:** won't work out of the box either. SAT-Accel's kernels contain free-running `while(true)` processes inside dataflow regions (e.g. `discover.cpp:73`, `clause_store_handler.cpp:552`), which sequential csim cannot run.
- **Decision (Stage 2):** for P1, a MiniSat stand-in reads the SAT-Accel image (option C). See the decision log.

## Pinned third-party code

| Component | Location | Version |
|---|---|---|
| SAT-Accel (Lo, Chang, Cong, FPGA'25) | `third_party/FPGA25_SAT_Accel` (submodule) | `37a0912f133d7b6038dcb291f347c634da6c8c34` |
| Xilinx HLS arbitrary-precision types (`ap_int.h`, open source) | `third_party/HLS_arbitrary_Precision_Types` (submodule) | `200a9aecaadf471592558540dc5a88256cbf880f`. Used only by the golden test. |
| MiniSat (niklasso/minisat) | `third_party/minisat` (submodule) | `37dc6c67e2af26379d88ce349eb9c4c6160e8543`. The P1 solver stand-in. Built with `-fpermissive`. |
| ESP (sld-columbia/esp) | torino: `~/esp`. Cloned 2026-10-16, detached at the tag, no submodules initialized. | tag `2026.1.0` = `a45f2bb8406350883c169505661bedf5b469e1a3` (2026-03-24). `main` was at `98317af` (2026-09-25); we pin the release for reproducibility. |

**ESP submodules.** None are initialized yet. The list below was taken from `main`; submodule commits at `2026.1.0` may differ. Fetch only what each stage needs:

| Submodule | Needed for |
|---|---|
| `accelerators/third-party/NV_NVDLA/sw` (`sld-columbia/nvdla-sw`, about 435 MB) | **Stage 1**: prebuilt `nvdla_compiler` (x86) and `nvdla_runtime` (riscv-linux) |
| `accelerators/third-party/NV_NVDLA/ip` (`sld-columbia/nvdla-hw`) | after P1, for the NVDLA tile in the SoC |
| `rtl/cores/ariane/ariane`, `soft/ariane/{linux,opensbi,riscv-pk}`, `rtl/caches/esp-caches` | after P1, for the Ariane SoC with Linux and the NVDLA runtime |
| `accelerators/stratus_hls/common/inc`, `catapult_hls/...`, `chisel/hw` | only if the SAT accelerator is rewritten in an ESP-native HLS flow |
| `soft/leon3/linux`, `rtl/cores/ibex/ibex`, `rtl/peripherals/bsg`, `utils/zynq`, `eigen`, `spandex-caches`, `riscv-tests` | not planned |

**ESP's LeNet for NVDLA.** ESP's third-party accelerator guide (`esp.cs.columbia.edu/docs/thirdparty_acc/`) provides:
- the model: `lenet_mnist.prototxt`, `lenet_mnist.caffemodel`, `lenet_mnist.json` (int8 calibration)
- a precompiled `lenet_mnist.nvdla` for nv_small
- a test image `seven.pgm`, with expected `nvdla_runtime --rawdump` output `0 2 0 0 0 0 0 124 0 0`

ESP's compile flags turned out to be `--cprecision int8 --configtarget nv_small --quantizationMode per-filter --informat nchw`. We use this network's architecture, with retrained weights (see Stage 1).

## Key facts about SAT-Accel (from source)

These facts determine what the serial baseline must reproduce.

### The host-built memory image
`src/host.cpp` `parseDIMACS` (lines 71–376) builds this image from DIMACS:

| Region | Layout | Allocated size |
|---|---|---|
| `clauseStore` | Clause literals in 4-word pages. The last word of each page is a next-page pointer. | `_HOST_MAX_CLAUSE_ELEMENTS` words |
| `cmd` | `{addressStart, numElements}`, 8 B per clause | `_FPGA_MAX_CLAUSES` |
| `clsStates` | `{xor of literals, remainingUnassigned}`, 8 B per clause | `_FPGA_MAX_CLAUSES` |
| `litStore` | Per-variable, per-polarity occurrence lists of clause IDs, in 16-word pages: 14 entries, a 0, then a next-page pointer | `_HOST_MAX_LITERAL_ELEMENTS` words |
| `lmd` | `ap_uint<512>` per variable, 64 B. Fields are given by the `LMD_*` macros in `data_structures.h`: phase, decision level, insert level, free space, latest page, numElements, addressStart, unitByLit, shortest clause length. | `numLiterals` (in SAT-Accel, "literals" means variables) |
| `answerStack` | Prefix = `decisionStore`, i.e. the variables of unit clauses plus variables in no clause. The count goes in `miscCounters[3]`. | `numLiterals` |

All buffers are migrated to the device in full for every instance (`host.cpp` 497–548). The host then polls completion once per second (561–590). That polling is a real T_sync cost in the native flow.

### Other facts
- **Fixed decisions.** `decide.cpp:12` (`WRITE_FIXED_DECISION`) applies `answerStack[0..fixedDecisionStackHeight)` as level-0 decisions. A resident base image plus an answerStack-prefix patch is therefore a plausible delta-transfer path for the later ReasonLink work. It is not used in P1.
- **Kernel-side copy-in.** `copy_in.cpp` copies the image in four dataflow loops: clsStates, litStore, lmd and answerStack.
- **Default build.** Vitis 2022.2 and XRT 2.14 on an Alveo U55C (HBM). Capacity is set by `_FPGA_MAX_LITERALS = 32768` and `_FPGA_MAX_CLAUSES = 131072` in `fpga_solver.h`. Software and hardware emulation run via `./runCompile doall`.

## Decision log
- **2026-10-16: platform.** Pivot from Chipyard/Gemmini to ESP with NVDLA nv_small and SAT-Accel. A VCU118 will be available later through ESP, not now.
- **2026-10-16: scope.** The current scope is P1 only. Later stages (ReasonLink RTL, the calibrated model, ESP SoC, board) will be planned after P1.
- **2026-10-16: where things run.** Local WSL handles the workload, image builder and baseline harness. The Columbia server handles ESP, Vitis/Vivado, ModelSim and the NVDLA compiler.
- **2026-10-16: Python venv location.** It lives outside the repo, at `~/venvs/reasonlink`, because I/O on `/mnt/c` is slow.
- **2026-10-16: GitHub.** The user approved pushing the `p1` branch to GitHub (public) so torino can `git clone` our code. Before this, commits were local only.
- **2026-10-16: classifier.** Use ESP's LeNet architecture and NVDLA flow (approved by the user). Its weights were retrained because ESP's demo weights reach only 74.5% under the runtime's preprocessing (see Stage 1).
- **2026-10-16: neural output used for the handoff.** The int8 ip2 logits, not the softmax output. In the NVDLA software stack, softmax is expected to run on the CPU as an emulator task; this is to be confirmed from the compiler output. Argmax does not need the softmax.

- **2026-10-16: SAT solver for P1 (option C, chosen by the user).** For P1, a functional stand-in replaces SAT-Accel's simulation. The solver backend receives the SAT-Accel memory image, decodes the clauses from `cmd` and `clause_store`, and solves them with MiniSat (`handoff/solver_backend.h`).
  - **Why:** torino has no `v++`/XRT, so SAT-Accel's `sw_emu` flow is out, and XRT needs root.
  - **Rejected for P1:** option A, a threaded C++ harness running SAT-Accel's free-running HLS kernels as threads (about half a day, with risk); option B, asking an admin to install XRT and the U55C platform.
  - **Consequence:** the P1 numbers for T_solve are MiniSat CPU time, not SAT-Accel cycles. The handoff side (conversion, bytes, transfers) is exact.
- **2026-10-16: serial baseline input.** The baseline starts from the in-memory CNF plus the predicted units, not from DIMACS text. We port SAT-Accel's image construction; text parsing is not part of the handoff we study.
  - The faithful mode clears the whole allocated buffers on each build, as `host.cpp` does per run.
  - `lean_clear` clears only the ranges written last time. It is there so the baseline is not a strawman.

- **2026-10-21: SAT-Accel functional simulation (P2, option A).** Run SAT-Accel's unmodified kernels as threads with a header shim, since torino has no `v++`/XRT. Zero-initialising `ap_axiu` in the shim is a deliberate fidelity choice: it reproduces the hardware path's stop semantics (see P2 Stage 1).

- **2026-10-22: P2 scope.** No Vitis (v++/XRT), Vitis HLS only; finish everything up to the board run; our own driver replaces XRT (user).
- **2026-10-22: separate tiles (user).** NVDLA keeps ESP's stock tile; ReasonLink lives in the SAT-Accel tile and reads NVDLA's output buffer from DRAM. A combined NVDLA+ReasonLink+SAT-Accel tile would give a true on-chip stream but was judged too costly.
- **2026-10-22: floating point stays.** SAT-Accel's double VSIDS math is kept; since ESP's third-party flow takes only Verilog/SV/VHDL file lists (no `.xci`), SAT-Accel goes to ESP as an out-of-context Vivado netlist (unisim primitives), which ESP's ModelSim flow simulates with its compiled Xilinx libraries.
- **2026-10-22: capacity.** Clauses and literal elements halved; variables kept at 32,768 because of the pqHandler heap bound (see P2-2).
- **2026-10-22: torino access.** I run torino jobs over SSH (key auth), asking before long runs.

- **2026-10-23: coherent DMA for the SAT-Accel tile.** ACC_COH_RECALL through the tile CSR, as ESP's NVDLA example does: correct without cache flushes, the simplest setup for the baselines.

- **2026-10-23: ReasonLink staged B then A (user).** Phase B first (hardware literal producer, on-chip FIFO, DRAM-level delta, overlap with NVDLA; SAT-Accel unmodified), then phase A (base formula resident on chip) if time allows.
- **2026-10-23: delta = fixed decisions.** Givens are passed as level-0 fixed decisions (answerStack prefix + miscCounters[3]) on a base image, instead of unit clauses: SAT-Accel's host conversion does this for unit clauses anyway, and it matches on all 1000 puzzles.
- **2026-10-23: logits from NVDLA.** The board loadable drops the Softmax layer so NVDLA outputs the int8 ip2 logits the handoff consumes (decision of 2026-10-16).
- **2026-10-23: baseline bitstream first (user),** so the baseline board run does not wait for ReasonLink.
- **2026-10-23: link the SAT-Accel checkpoint in implementation** (`read_checkpoint -cell` as an `opt_design` pre-hook) rather than renaming its top; both bitstreams share one SoC synthesis.
- **2026-10-30: APB reads with ESP's timing.** `apb2axil` accepts reads without a setup phase; the RTL testbench uses ESP's timing by default.
- **2026-10-30: Milestone 2 = resident base via a kernel patch (user: go ahead).** It is the proposal's mechanism (iii) in full, so the goal is unchanged. Restore only what a solve changes; keep clause states and literal metadata as DRAM copies for now (simple, exact), on-chip restore of those is a later option.
- **2026-10-31: bitstreams from the Milestone 2 netlists.** With `residentBase` = 0 they behave as the baseline, so one pair of bitstreams covers every handoff mode (full, delta, ReasonLink, each with or without `--resident`).
- **2026-11-04: non-coherent DMA dropped (user).** Coherent DMA (`ACC_COH_RECALL`) is the design's mode; the non-coherent checks are not needed.
- **2026-11-04: board run with 100 puzzles, not 1000 (user: a smaller number is fine).** NVDLA takes about 0.24 s per cell under Linux, so 1000 puzzles would take about a day.
- **2026-11-04: Linux on the board, interrupt kept with a polling fallback (user approved).** The UMD sleeps on the interrupt for up to 1 s, then polls `ap_done`; a missed interrupt costs time but not correctness.
- **2026-11-05: board run order (user).** Serial configurations first (they never hung), then each double-buffered one in its own boot; reprogram the FPGA after any hang, since a soft reset leaves NVDLA wedged.
- **2026-11-05: debug ReasonLink before the double-buffered runs (user).** The double-buffered ReasonLink runs need the fix too.
- **2026-11-05: ReasonLink layout fix in the driver, not the RTL.** With NVDLA's 8 B surface stride the contiguous layout reads exactly the right bytes, and that is the path the RTL test verified; no new bitstream. Unrepresentable strides are rejected instead of rounded.
- **2026-11-05: no SSH to the board during measured runs.** Each login costs the board's CPU about 1-1.5 s and lands in the run (it made ReasonLink look 9% slower). Runs go detached; results are fetched after the expected run time. Earlier runs are reported with the stalls removed (per-puzzle NVDLA time above the run's median per cell).
- **2026-11-05: board stage closed (user).** All 13 configurations ran correct. No separate board results note (user); the Linux image rebuild is next (ask first). Board logs are committed with `git add -f` (`.gitignore` excludes `*.log`).
- **2026-11-05: test binaries pushed into the board's RAM.** A Linux image rebuild is not needed to test a fix: `rl_board` builds on torino in `~/m2/rldbg` (linked with the existing objects) and goes to the board as base64. The initramfs copy is updated at the next image rebuild.
- **2026-10-31: keep the routed designs, fix the constraints, gate the bitstream on timing.** The wrong clock changed what was timed, not the netlist, so re-timing the routed checkpoints with correct constraints is valid and saves two 1.7 h runs. `top.bit` was accepted at -0.059 ns (4 endpoints in Ariane's FPU, not in our logic); a fresh implementation with the fixed hooks may close it.

## Pipeline: what still needs to be optimized or figured out (2026-11-05)

The board run removes the handoff (H1: 34% of the faithful run; under 0.05% with the delta or ReasonLink), but the result is **not yet a good producer-consumer pipeline**. Three reasons, each a part to optimize or figure out:

1. **The producer is about 800x slower than the consumer.** Every given cell is its own NVDLA inference through Linux's NVDLA runtime: 79.3 ms per cell, about 2.5 s per puzzle, against a 3-10 ms solve. NVDLA is 99% of a delta or ReasonLink run, so the pipeline is NVDLA-bound and any overlap can hide at most the solve.
   - *To optimize:* classify all of a puzzle's givens in one NVDLA submission (a batch, or the cells packed into one input tensor). Needs a new loadable (`workload/nvdla_compile.sh`), `rl_nvdla.cpp` changes, and ReasonLink pushes that index cells inside one output tensor (per-cell `LOGIT_ADDR` already allows any 8-byte-aligned address). This would cut T_nn by a large factor and bring the handoff share back up, which is what gives ReasonLink's mechanisms something to show.
2. **The CPU is in the loop for every cell.** Per cell it submits the NVDLA task, waits for it, pushes {cell, address} to ReasonLink and polls for the credit before the next submit (NVDLA reuses one output buffer). The ReasonLink core converts a cell in about 85 tile cycles, but the push and credit cost about 18 us per cell over ESP's APB, so ReasonLink's exposed handoff (about 0.7 ms per puzzle) is no better than the CPU delta's (about 0.5 ms). There is no direct NVDLA-to-ReasonLink stream; both go through DRAM (separate tiles, decision of 2026-10-22).
   - *To optimize:* one push per puzzle instead of per cell once NVDLA writes all cells into one tensor (ReasonLink walks the cells itself; also removes the per-cell credit); or several output buffers so NVDLA can run ahead of the tile. *To figure out:* whether ReasonLink can watch NVDLA's completion itself instead of the CPU relaying it.
3. **The 79 ms per cell is probably software, not compute.** LeNet on nv_small should take far less; most of it is likely NVDLA's user-mode runtime and its kernel driver, whose task scheduler ("firmware") runs on the 78 MHz Ariane and handles hundreds of events per task.
   - *To figure out:* the split between NVDLA hardware time and software time per task (e.g. timestamps around `submit` against the driver's per-layer events, or NVDLA's own counters), and then per-accelerator utilization, which the proposal asks for (only SAT-Accel's busy time is measured now).

**Data still missing for the evaluation plan:**
- **Repeat runs.** Every configuration ran once; ReasonLink vs the CPU delta differ by less than one run's noise (250.2 vs 250.7 s). About 3 runs of the key configurations (about 1.5 h on the board, no SSH during the runs).
- **FIFO-depth and transfer-granularity sweeps** (proposal). With one NVDLA output buffer, ReasonLink takes one cell at a time, so these sweeps show nothing until item 2 changes.
- **NVDLA's hardware utilization** (item 3).
- **Size of NVDLA's logit differences** from the int8 emulator (925 of 3,141 cells differ; digits identical). Minor.

## Open issues / blockers
- **NVDLA hangs mid-task (2026-11-05, twice).** Double-buffered lean at about puzzle 75, and serial ReasonLink + resident at about puzzle 15: `rl_board` waits forever in `nvdla_task_submit`, and NVDLA and the PLIC show no pending interrupt, so NVDLA stalled mid-task. The board then slowly stops answering (`dmesg`, then SSH); the 11-04 night's run probably ended the same way. Both hangs were in runs where the SAT-Accel tile does DMA while NVDLA runs, and both while I polled the board over SSH. The later double-buffered batch (500 puzzles with that overlap, no SSH during the runs) did not hang, so Ethernet/SSH activity during an NVDLA task is as likely a trigger as the overlap; not proven either way. Recovery: reprogram the FPGA.
- **ReasonLink's layout fix is not in the Linux image yet:** the 100-puzzle reruns use `/root/rl_board_fix`, pushed into the board's RAM; a reboot loses it until the image is rebuilt.
- **EDCL loads are unreliable** (1-7 attempts per boot; once the final reset was lost). Retried by `~/m2/boot_retry.sh`.
- **dubois20** takes a slightly different search path in RTL than in the functional model (352/175 vs 370/177 decisions/conflicts); likely the `FPGA_HW` code paths, not verified.
- **NVDLA vs the int8 emulator:** on the board, 925 of 3,141 given cells (100 puzzles) have logits that differ from the emulator, but no digit differs (grids identical to the trace run on 100/100). The emulator was expected to be bit-exact; the size of the logit differences is not yet quantified.
- **Board order:** a bare-metal run built before 2026-11-04 leaves the SAT-Accel interrupt blocked, and a hung accelerator survives a soft reset; reprogram the FPGA in both cases.
- **Windows checkouts.** Git's `core.autocrlf=true` turns SAT-Accel's DIMACS benchmarks into CRLF files, which its parser cannot read. `run_golden.sh` strips the CRs; a Linux checkout (torino) is unaffected.
