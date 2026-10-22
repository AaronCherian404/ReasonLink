#!/usr/bin/env bash
# SAT-Accel functional simulation checks. Writes results/p2/:
#   sataccel_benchmarks.txt   SAT-Accel's own benchmark CNFs, expected SAT/UNSAT
#   sataccel_sudoku.csv/.txt  all Sudoku puzzles vs the PySAT reference
# Usage (from sataccel_sim/): bash run_check.sh [num_puzzles]
set -uo pipefail
cd "$(dirname "$0")"
BIN=build/sataccel_check
TESTS=../third_party/FPGA25_SAT_Accel/SAT_test_cases
DATA=../data/sudoku
OUT=../results/p2
CASES=build/cases
mkdir -p "$OUT" "$CASES"

# Benchmarks: our P1 golden-test set plus the instances SAT-Accel's own
# testcases_sim.sh uses. Copies are LF-normalised (see PROGRESS.md: CRLF).
BENCH=(
  sat/quinn.dimacs:1 sat/aim50.dimacs:1 sat/nqueens_4.dimacs:1 sat/nqueens_8.dimacs:1
  sat/4blocks.dimacs:1 sat/aalto.dimacs:1 sat/sudoku_4_2_2.dimacs:1 sat/sudoku_6_2_3.dimacs:1
  sat/bmc-ibm-2.dimacs:1 sat/bmc-ibm-5.dimacs:1
  unsat/aalto_2.dimacs:0 unsat/nqueens_3.dimacs:0 unsat/dubois20.dimacs:0 unsat/qg6-10.dimacs:0
  satlib/hole7_unsat.cnf:0 satlib/aim-200-1_6-no-1.cnf:0
)
pass=0; fail=0
{
  echo "SAT-Accel functional simulation (threaded sw_emu path) on SAT-Accel benchmarks"
  for entry in "${BENCH[@]}"; do
    f="${entry%:*}"; expect="${entry##*:}"
    [ -f "$TESTS/$f" ] || { echo "SKIP   $f (not in repo)"; continue; }
    local_copy="$CASES/$(echo "$f" | tr / _)"
    tr -d '\r' < "$TESTS/$f" > "$local_copy"
    if "$BIN" cnf "$local_copy" "$expect"; then pass=$((pass+1)); else fail=$((fail+1)); fi
  done
  echo "passed $pass, failed $fail"
} 2>&1 | tee "$OUT/sataccel_benchmarks.txt"

"$BIN" sudoku "$DATA/base.cnf" "$DATA/trace.bin" "$DATA/reference.bin" "$OUT/sataccel_sudoku.csv" "${1:-0}" \
  | tee "$OUT/sataccel_sudoku.txt"
