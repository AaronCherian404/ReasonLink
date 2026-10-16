#!/usr/bin/env bash
# Byte-exact golden test: SAT-Accel's original parseDIMACS vs. our C port.
# Run via `make golden` from handoff/. Writes results/p1/golden_test.txt.
set -uo pipefail

HANDOFF="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$HANDOFF/.." && pwd)"
BUILD="$HANDOFF/build"
WORK="$BUILD/golden"
CASES="$WORK/cases"
SAT_TESTS="$REPO/third_party/FPGA25_SAT_Accel/SAT_test_cases"
CONFIG="$REPO/third_party/FPGA25_SAT_Accel/src/configuration.json"
REPORT="$REPO/results/p1/golden_test.txt"
REGIONS="clause_store cmd lit_store answer_stack cls_states misc lmd"

rm -rf "$WORK" && mkdir -p "$CASES"
"${PY:-python3}" "$REPO/workload/export_cnf.py" --out "$CASES" --puzzles 0 1 2 --unsat 1 > /dev/null || exit 1
for f in sat/aim50.dimacs sat/nqueens_4.dimacs sat/quinn.dimacs sat/4blocks.dimacs \
         unsat/dubois20.dimacs unsat/nqueens_3.dimacs unsat/aalto_2.dimacs \
         satlib/hole7_unsat.cnf satlib/aim-200-1_6-no-1.cnf; do
    # The committed files use LF; a Windows checkout with core.autocrlf=true
    # turns them into CRLF, which host.cpp's tokenizer cannot parse ("0\r").
    tr -d '\r' < "$SAT_TESTS/$f" > "$CASES/$(echo "$f" | tr / _)"
done

pass=0; fail=0
{
echo "Golden test: SAT-Accel parseDIMACS (host.cpp @ $(git -C "$REPO/third_party/FPGA25_SAT_Accel" rev-parse --short HEAD)) vs handoff/sataccel_image.c"
echo "Regions compared byte for byte (full allocated buffers): $REGIONS"
echo
printf "%-40s %-6s %s\n" case result detail
for cnf in "$CASES"/*; do
    name="$(basename "$cnf")"
    g="$WORK/golden_$name"; o="$WORK/ours_$name"
    mkdir -p "$g" "$o"
    if ! "$BUILD/golden_parse" "$cnf" "$CONFIG" "$g" > "$g.log" 2>&1; then
        printf "%-40s %-6s %s\n" "$name" SKIP "original parser failed: $(tail -1 "$g.log")"; continue
    fi
    "$BUILD/image_tool" "$cnf" "$o" > "$o.log" 2>&1
    bad=""
    for r in $REGIONS; do cmp -s "$g/$r.bin" "$o/$r.bin" || bad="$bad $r"; done
    grep -q "decode check: ok" "$o.log" || bad="$bad decode"
    dims="$(grep -o 'vars=[0-9]* clauses=[0-9]*' "$o.log")"
    if [ -z "$bad" ]; then pass=$((pass+1)); printf "%-40s %-6s %s\n" "$name" PASS "$dims"
    else fail=$((fail+1)); printf "%-40s %-6s %s\n" "$name" FAIL "differs:$bad"; fi
done
echo
echo "passed $pass, failed $fail"
} > "$REPORT"
cat "$REPORT"
grep -q "failed 0$" "$REPORT"
