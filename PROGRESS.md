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

**Status:** functional check done (2026-10-21). Still to do in this stage: SAT-Accel as a backend option in the baselines (in place of the MiniSat stand-in), and a cross-check build against Vitis HLS 2023.2's headers on torino.

### Reproduce (local WSL, after P1 Stages 1–2)
```
cd sataccel_sim
make            # build/sataccel_check
make check      # about 8 min; writes results/p2/sataccel_{benchmarks.txt,sudoku.csv,sudoku.txt}
```
`HLS_INC=/path/to/Vitis_HLS/<ver>/include make` builds against vendor headers instead of `shim/`.

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
| 1,000 Sudoku puzzles | SAT/UNSAT matches PySAT on **1000/1000**. Grids: 991 identical, 9 valid alternative solutions, 0 wrong. Mean 1.44 decisions and 0.14 conflicts per puzzle, 12 ms simulation time. |

Times are simulation wall time on the laptop, not hardware cycles.

### Problem found: uninitialised `read_nb` packet in SAT-Accel's sw_emu restart kernel
- **Symptom.** `4blocks`, `qg6-10` and `hole7` hung: `restartCalculator` and `pqHandler` had exited, and the solver was blocked reading `restartValueStream`.
- **Cause.** `restart.cpp`'s sw_emu path does `ap_axiu<1,0,0,0> pkt; stop.read_nb(pkt); if (pkt.data) break;`. When the stream is empty, `pkt.data` is uninitialised (`ap_uint`'s default constructor does not zero it). Stack garbage reads as "stop", so the kernel exits early and writes its final 0. The solver then consumes that 0 as a restart value at its next restart, and its end-of-solve flush (`FLUSH_RESTART_*`, waiting for a 0) blocks forever. Only instances that reach a restart were affected.
- **Fix.** `shim/ap_axi_sdata.h` zero-initialises all `ap_axiu`/`ap_axis` fields, so an empty `read_nb` means "no stop". This matches the `FPGA_HW` path, which stops on a blocking read of a real stop message. The kernel sources stay unmodified.
- **Caveat.** A build with `HLS_INC` (vendor headers) may still hit this, depending on whether Vitis's `ap_axiu` zero-initialises.

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

## Open issues / blockers
- **The baselines have not run on Ariane yet.** The binary is ready (`make riscv`), but there is no ESP SoC simulation or board yet. All P1 times are host x86.
- **SAT-Accel in the baselines.** SAT-Accel now runs in functional simulation (P2 Stage 1, option A), but the baselines still use the MiniSat stand-in until the backend option is wired in. Simulation gives functional results and search counters, not cycles; cycle-level numbers still need hw_emu/RTL or the board.
- **Toolchain version.** The server has Vitis HLS 2023.2, but SAT-Accel targets 2022.2. Pragmas or headers may need small fixes when we build it.
- **Windows checkouts.** Git's `core.autocrlf=true` turns SAT-Accel's DIMACS benchmarks into CRLF files, which its parser cannot read. `run_golden.sh` strips the CRs; a Linux checkout (torino) is unaffected.
