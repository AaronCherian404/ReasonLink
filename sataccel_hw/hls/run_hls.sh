#!/usr/bin/env bash
# Synthesize all seven SAT-Accel kernels with Vitis HLS, in parallel.
# Kernel/source grouping is SAT-Accel's (runCompile.sh). Outputs:
#   $OUT/<kernel>/sol/impl/ip/   Vivado IP (Verilog), used by sataccel_hw/rtl
#   $OUT/<kernel>.log            Vitis HLS log
# Then summarize_hls.py collects latency and resources.
#
# Usage (from sataccel_hw/hls/): bash run_hls.sh [kernel ...]
# Env: SRC (default: patched copy, see ../Makefile), OUT, PERIOD_NS, PART
set -uo pipefail
cd "$(dirname "$0")"
SRC="${SRC:-$(realpath ../build/src)}"
OUT="${OUT:-$(realpath -m ../build/hls)}"
PERIOD_NS="${PERIOD_NS:-10}"            # 100 MHz; ESP's VCU118 SoC runs at 78 MHz
PART="${PART:-xcvu9p-flga2104-2L-e}"    # VCU118
mkdir -p "$OUT"

declare -A FILES=(
  [solver]="backtrack.cpp color.cpp copy_in.cpp decide.cpp discover.cpp learn.cpp minimize.cpp manage.cpp solver.cpp"
  [clause_store_handler]="clause_store_handler.cpp"
  [location_handler]="location_handler.cpp"
  [restartCalculator]="restart.cpp"
  [timer]="timer.cpp"
  [pqHandler]="priority_queue_functions.cpp pq_handler.cpp"
  [message]="message.cpp"
)
KERNELS=("$@")
[ ${#KERNELS[@]} -eq 0 ] && KERNELS=(solver clause_store_handler location_handler restartCalculator timer pqHandler message)

pids=()
for k in "${KERNELS[@]}"; do
  [ -n "${FILES[$k]:-}" ] || { echo "unknown kernel $k"; exit 1; }
  ( /usr/bin/time -f "$k: %e s, max RSS %M KB" \
      vitis_hls -f "$(realpath hls_kernel.tcl)" -tclargs "$k" "$OUT" "$PERIOD_NS" "$PART" "$SRC" ${FILES[$k]} \
      > "$OUT/$k.log" 2>&1
    echo "$k exit=$? $(tail -1 "$OUT/$k.log")" ) &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
