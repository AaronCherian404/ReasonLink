# ReasonLink

**Characterizing and optimizing the handoff between neural and symbolic accelerators.**

In a neuro-symbolic pipeline, a neural accelerator produces predictions that a symbolic accelerator then has to consume. Between them sits a *handoff*: the predictions are converted into the solver's format, moved through memory, and the two sides synchronize. Once both accelerators are fast, this glue can dominate end-to-end time.

ReasonLink measures that handoff and designs a data-movement-aware interface to shrink it. The interface combines:
- a compact literal stream
- on-chip buffering
- delta transfers
- producer–consumer overlap

**Setup**
- **Platform:** [ESP](https://www.esp.cs.columbia.edu) SoC
- **Neural accelerator:** NVDLA (nv_small, int8) running LeNet
- **Symbolic accelerator:** [SAT-Accel](https://github.com/milo168/FPGA25_SAT_Accel), a CDCL SAT solver
- **Workload:** visual Sudoku, where handwritten digits are classified and the givens become unit clauses

The full proposal and the P1 report are in [`Project_Title__Identify_the_Problem_and_Core_Idea/`](Project_Title__Identify_the_Problem_and_Core_Idea/). [`PROGRESS.md`](PROGRESS.md) is the running log of results, decisions, and open issues.

**Status:** P1 (infrastructure and baselines) is in progress. See the stage table in `PROGRESS.md`.

## Repository layout

```
workload/     Sudoku CNF encoder, puzzle generator, int8 NVDLA LeNet emulator, training, reference solve
handoff/      SAT-Accel memory-image builder (C), solver backend, golden test, tools
baselines/    serial and double-buffered handoff baselines with cycle counters (x86 / RISC-V)
models/       retrained LeNet (Caffe model + int8 calibration) for NVDLA
results/p1/   machine-readable results
env/          environment setup and server inventory scripts
third_party/  pinned submodules: SAT-Accel, MiniSat, Xilinx ap_int headers
```

## Requirements

| Where | What | Used for |
|---|---|---|
| Any Linux machine (or WSL) | git, Python 3.10+, gcc/g++, make | workload, image builder, solver, tests |
| **Machine with the ESP platform** | [ESP](https://github.com/sld-columbia/esp) release `2026.1.0` with the `NV_NVDLA/sw` submodule, plus ESP's CAD tools: Vivado 2023.2, ModelSim DE 2023.2 or Xcelium, and the RISC-V toolchain | compiling the classifier for NVDLA, and all later SoC, simulation, and FPGA work |

**Access to an ESP setup is required** for the NVDLA steps. The local steps run without it. We use a Columbia server that has ESP's toolchain installed.

## Running

### 1. Clone and set up (local)
```bash
git clone --recursive https://github.com/AaronCherian404/ReasonLink.git
cd ReasonLink
bash env/setup_wsl.sh                  # creates ~/venvs/reasonlink with pinned deps
source ~/venvs/reasonlink/bin/activate
```

### 2. Workload and classifier (local, ~2 min)
```bash
bash workload/fetch_esp_lenet.sh       # ESP's NVDLA LeNet assets (md5-checked)
python workload/nvdla_lenet.py --model esp --check   # int8 emulator vs ESP's NVDLA output
python workload/train_lenet.py         # retrain -> models/rl_lenet/ (already committed)
python workload/dataset.py             # 1,000 puzzles + NVDLA output trace -> data/sudoku/
python workload/reference_solve.py     # PySAT reference -> results/p1/
```

### 3. SAT-Accel image and solver (local)
```bash
cd handoff
make                 # build tools
make golden          # byte-exact check vs SAT-Accel's own host code
make solvecheck      # trace -> SAT-Accel image -> solver on all puzzles
```

### 4. Baselines (local)
```bash
cd baselines
make run             # serial + double-buffered, faithful + lean, 1,000 puzzles each
                     # -> results/p1/baseline_*.csv, baselines_summary.{json,md}
make riscv           # static RISC-V Linux binary for Ariane (ESP's RISC-V toolchain)
```

### 5. NVDLA loadable (on the ESP machine)
```bash
git clone https://github.com/sld-columbia/esp.git ~/esp
cd ~/esp && git checkout 2026.1.0
git submodule update --init accelerators/third-party/NV_NVDLA/sw
cd ~/ReasonLink && ESP_ROOT=~/esp bash workload/nvdla_compile.sh
```
The script first recompiles ESP's demo network and checks that the result matches ESP's shipped loadable byte for byte. It then compiles our classifier to `build/nvdla/rl_lenet.nvdla`.

## Notes
- **Windows:** git with `core.autocrlf=true` converts SAT-Accel's benchmark files to CRLF. The golden test handles this; a Linux checkout is unaffected.
- **P1 solver stand-in:** SAT-Accel needs Vitis `v++` and XRT to run. For P1, a MiniSat backend solves the exact memory image SAT-Accel would receive. See `PROGRESS.md`.
