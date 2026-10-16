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
| 0 | Repo scaffolding, local env, SAT-Accel submodule, server inventory, ESP clone + RISC-V toolchain | **Local part done. Server part blocked on SSH details.** | see `git log` |
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

### Columbia server (pending)
- **Inventory:** `ssh <host> 'bash -s' < env/server_probe.sh`, then paste the output below.
- **Credentials:** never commit them. If needed, local settings go in `env/server.env`, which is gitignored.
- **Planned on the server:**
  - Clone ESP with submodules at a pinned release, and record the commit here.
  - Run `./utils/toolchain/build_riscv_toolchain.sh`.
  - SAT-Accel software emulation.
  - The NVDLA compiler.

_Inventory: not yet collected._

## Pinned third-party code

| Component | Location | Version |
|---|---|---|
| SAT-Accel (Lo, Chang, Cong, FPGA'25) | `third_party/FPGA25_SAT_Accel` (submodule) | `37a0912f133d7b6038dcb291f347c634da6c8c34` |
| ESP (sld-columbia/esp) | server, path TBD | TBD (latest release seen: 2026.1.0) |

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
- **Server access needed.** SSH details are required for the server inventory, the ESP clone and toolchain, and Vitis software emulation.
- **Vitis version.** If the server's Vitis is not 2022.2, SAT-Accel's `runCompile.sh` paths and possibly its pragmas may need porting. This is to be assessed in Stage 2.
