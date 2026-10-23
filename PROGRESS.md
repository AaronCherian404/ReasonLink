# ReasonLink progress log

Running record of what is built, what was decided, and what is blocked. Update at the end of every stage.

**Current target:** P1, due Oct 19, 2026 ("Infrastructure and baselines").
**Branch:** `p1`, pushed to `github.com/AaronCherian404/ReasonLink` (public). torino uses a clone of it at `~/ReasonLink`.

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

**P1 report:** `Project_Title__Identify_the_Problem_and_Core_Idea/neural-symbolic-p1.tex` (IEEEtran; 2 pages total: about 1.4 pages of body, then Appendix A and references). Its numbers come from this file. Build it with `latexmk -pdf` in that folder; locally, IEEEtran.bst must be on BSTINPUTS (Overleaf has it). It covers what was built, the results (functional + baseline breakdown), limitations and open issues, the next steps, and (Appendix A) every reproduction step.

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
| P2-4 | SAT-Accel as an ESP third-party tile; Linux driver + library; bare-metal test; VCU118 SoC (Ariane, NVDLA, SAT-Accel); Linux image; full-SoC simulation | **In progress**: everything builds; full-SoC simulation running | branch `p2` |

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

### P2-4: SAT-Accel in an ESP SoC (in progress)
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

## Open issues / blockers
- **The baselines have not run on Ariane yet.** The binary is ready (`make riscv`), but there is no ESP SoC simulation or board yet. All P1 times are host x86.
- **SAT-Accel in the baselines.** SAT-Accel now runs in functional simulation (P2 Stage 1, option A), but the baselines still use the MiniSat stand-in until the backend option is wired in. Simulation gives functional results and search counters, not cycles; cycle-level numbers still need hw_emu/RTL or the board.
- **Toolchain version.** The server has Vitis HLS 2023.2, but SAT-Accel targets 2022.2. Pragmas or headers may need small fixes when we build it.
- **Windows checkouts.** Git's `core.autocrlf=true` turns SAT-Accel's DIMACS benchmarks into CRLF files, which its parser cannot read. `run_golden.sh` strips the CRs; a Linux checkout (torino) is unaffected.
