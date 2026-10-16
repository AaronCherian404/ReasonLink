# ReasonLink progress log

Running record of what is built, what was decided, and what is blocked. Update at the end of every stage.

**Current target:** P1, due Oct 19, 2026 ("Infrastructure and baselines").
**Branch:** `p1`, with local commits only. One commit per stage.

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
| 1 | Sudoku CNF, dataset, int8 LeNet + score traces, NVDLA compiler flow, pysat reference | not started | |
| 2 | Standalone SAT-Accel image builder + byte-exact golden test; SAT-Accel software emulation on the server | not started | |
| 3 | Serial + double-buffered baselines end to end with cycle counters; RISC-V build | not started | |

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

## Open issues / blockers
- **SAT-Accel simulation route (Stage 2).** Neither `v++` nor XRT is on torino, so SAT-Accel's native `sw_emu` flow is unavailable. The route will be decided in Stage 2. Candidates:
  - a threaded C++ harness that runs SAT-Accel's kernels as threads, using the HLS stream headers from Vitis HLS 2023.2
  - an RTL export with ModelSim co-simulation, which is heavy
  - pysat as the functional stand-in for P1
- **Toolchain version.** The server has Vitis HLS 2023.2, but SAT-Accel targets 2022.2. Pragmas or headers may need small fixes.
