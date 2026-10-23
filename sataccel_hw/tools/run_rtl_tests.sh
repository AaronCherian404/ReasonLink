#!/usr/bin/env bash
# Run SAT-Accel RTL tests in xsim (after `make bd sim-setup tb_tool`).
# Each test: tb_tool builds the image and records the driver's register
# accesses; xsim replays them against the RTL; tb_tool checks the result.
#
# Usage (from sataccel_hw/): bash tools/run_rtl_tests.sh <out_dir> <spec>...
#   sudoku:<k>            puzzle k of ../data/sudoku (prediction trace + reference)
#   cnf:<file>:<1|0>      a DIMACS file and its expected SAT/UNSAT
# Env: TB_MEM=used FILL=<hex64> loads only the image's used ranges and makes
#      every other word read as FILL (checks SAT-Accel reads nothing else)
# Writes <out_dir>/<test>/{ops,mem,dump}.txt, sim.log, and <out_dir>/summary.txt
set -uo pipefail
cd "$(dirname "$0")/.."
OUT="$(realpath -m "$1")"; shift
DATA="$(realpath ../data/sudoku)"
SIMDIR=build/vivado_bd/sataccel_bd.sim/sim_1/behav/xsim
[ -x build/tb_tool ] || { echo "build/tb_tool missing (make tb_tool)"; exit 1; }
[ -d "$SIMDIR/xsim.dir/tb_sataccel_behav" ] || { echo "xsim snapshot missing (make sim-setup)"; exit 1; }
mkdir -p "$OUT"

for spec in "$@"; do
  case "$spec" in
    sudoku:*) k="${spec#sudoku:}"; name="sudoku_$k"; dir="$OUT/$name"; mkdir -p "$dir"
              build/tb_tool gen-sudoku "$DATA" "$k" "$dir" || continue ;;
    cnf:*)    rest="${spec#cnf:}"; f="${rest%:*}"; exp="${rest##*:}"
              name="$(basename "$f" | sed 's/\.[a-z]*$//')"; dir="$OUT/$name"; mkdir -p "$dir"
              tr -d '\r' < "$f" > "$dir/input.cnf"
              build/tb_tool gen-cnf "$dir/input.cnf" "$exp" "$dir" || continue ;;
    *) echo "bad spec $spec"; continue ;;
  esac
  t0=$(date +%s)
  ( cd "$SIMDIR" && xsim tb_sataccel_behav -R \
      --testplusarg "ops=$dir/ops.txt" --testplusarg "mem=$dir/mem.txt" --testplusarg "dump=$dir/dump.txt" \
      ${FILL:+--testplusarg "fill=$FILL"} \
      > "$dir/sim.log" 2>&1 )
  t1=$(date +%s)
  res=$(build/tb_tool check "$dir")
  ctr=$(grep -h "^COUNTERS done" "$dir/sim.log" | sed 's/^COUNTERS done //')
  [ -n "$ctr" ] || ctr="(no counters: $(grep -h -m1 -E 'TIMEOUT|ERROR|Fatal' "$dir/sim.log"))"
  echo "$res | $ctr | wall=$((t1 - t0))s" | tee -a "$OUT/summary.txt"
done
