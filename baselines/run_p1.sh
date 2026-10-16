#!/usr/bin/env bash
# Run both baselines in the faithful and lean configurations on the full
# dataset, then check and summarize. Outputs in results/p1/:
#   baseline_<mode>_<config>.csv   per-puzzle cycles per phase
#   baselines_summary.{json,md}    medians, handoff fraction, correctness checks
#
#   faithful: clear the full allocated buffers + migrate them in full (host.cpp)
#   lean:     clear/transfer only what the previous/current image uses
#
# Usage (from baselines/): bash run_p1.sh [path/to/rl_baselines]
set -euo pipefail
cd "$(dirname "$0")"
BIN="${1:-build/native/rl_baselines}"
DATA="../data/sudoku"
OUT="../results/p1"
PY="${PY:-$HOME/venvs/reasonlink/bin/python}"
mkdir -p "$OUT"

for mode in serial double; do
    "$BIN" --mode "$mode" --clear full --xfer allocated --data "$DATA" --csv "$OUT/baseline_${mode}_faithful.csv"
    "$BIN" --mode "$mode" --clear lean --xfer used      --data "$DATA" --csv "$OUT/baseline_${mode}_lean.csv"
done
"$PY" summarize.py "$OUT"
