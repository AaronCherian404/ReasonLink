# ReasonLink progress log

Running record of what is built, what was decided, and what is blocked. Update at the end of every stage.

**Current target:** P1, due Oct 19, 2026 ("Infrastructure and baselines").
**Branch:** `p1`, pushed to `github.com/AaronCherian404/ReasonLink` (public). torino uses a clone of it at `~/ReasonLink`.

## P1 definition (adapted to ESP + NVDLA)

The proposal's P1 calls for three things. On the new platform they mean:

1. **The neural accelerator runs the classifier.** We train an int8 LeNet. The NVDLA (nv_small) compile flow is set up on the server. Per-cell int8 score traces stand in for NVDLA's output buffer in DRAM.
2. **The SAT solver solves Sudoku CNFs in simulation.** This uses SAT-Accel software emulation on the server if Vitis 2022.2 is available. Otherwise pysat is the functional stand-in, and the blocker is recorded.
3. **Both baselines run end to end with cycle counters.** The serial and double-buffered handoffs follow SAT-Accel's native host flow:
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

## Stage status

| Stage | Scope | Status | Commit |
|---|---|---|---|
| 0 | Repo scaffolding, local env, SAT-Accel submodule, server inventory, ESP clone (the toolchain is preinstalled on the server) | **Done** (2026-10-16) | see `git log` |
| 1 | Sudoku CNF, dataset, int8 LeNet + score traces, NVDLA compiler flow, pysat reference | **Done** (2026-10-16) | see `git log` |
| 2 | Standalone SAT-Accel image builder + byte-exact golden test; SAT-Accel software emulation on the server | not started | |
| 3 | Serial + double-buffered baselines end to end with cycle counters; RISC-V build | not started | |

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
- **Next step:** the "SAT solver in simulation" route will be decided in Stage 2.

## Pinned third-party code

| Component | Location | Version |
|---|---|---|
| SAT-Accel (Lo, Chang, Cong, FPGA'25) | `third_party/FPGA25_SAT_Accel` (submodule) | `37a0912f133d7b6038dcb291f347c634da6c8c34` |
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

The compile command is `nvdla_compiler --prototxt … --caffemodel … --calibtable … --cprecision int8 --configtarget nv_small`. **Proposed for Stage 1:** use this network as the Sudoku digit classifier instead of training our own LeNet, so the score traces come from the exact network NVDLA runs.

## Key facts about SAT-Accel (from source)

These facts determine what the serial baseline must reproduce.

### The host-built memory image
`src/host.cpp` `parseDIMACS` (lines 71–330) builds this image from DIMACS:

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

## Open issues / blockers
- **SAT-Accel simulation route (Stage 2).** Neither `v++` nor XRT is on torino, so SAT-Accel's native `sw_emu` flow is unavailable. The route will be decided in Stage 2. Candidates:
  - a threaded C++ harness that runs SAT-Accel's kernels as threads, using the HLS stream headers from Vitis HLS 2023.2
  - an RTL export with ModelSim co-simulation, which is heavy
  - pysat as the functional stand-in for P1
- **Toolchain version.** The server has Vitis HLS 2023.2, but SAT-Accel targets 2022.2. Pragmas or headers may need small fixes.
