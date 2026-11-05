#!/bin/sh
# Board run, remaining configurations (run_baselines.sh's commands, 100 puzzles),
# one batch per call: sh rl_batch.sh serial | double_lean | double_delta | ...
# Log: /root/rl_<batch>.log; CSVs in results/ as run_baselines.sh names them.
RL=/root/CU_SATACCEL/rl
cd $RL
dmesg -n 1
kill $(pidof klogd) 2>/dev/null
# Bring-up checks (as in run_baselines.sh); stop if either accelerator is not
# healthy. esplink --reset resets the cores only: after an accelerator hang,
# NVDLA stays wedged until the FPGA is reprogrammed (2026-11-05)
irqs() { awk '/sataccel/ {print $2}' /proc/interrupts; }
i0=$(irqs)
sa=$(/root/CU_SATACCEL/sataccel_run cnf "$RL/data/base.cnf" 1); echo "$sa"
echo "SAT-Accel interrupts: $i0 -> $(irqs)"
nv=$(cd /root/NV_NVDLA && ./nvdla_runtime --loadable lenet_mnist.nvdla --image seven.pgm --rawdump > /dev/null && cat output.dimg)
echo "NVDLA seven.pgm: $nv (ESP's published output: 0 2 0 0 0 0 0 124 0 0)"
echo "$sa" | grep -q "^1/1 correct" && [ "$(echo $nv)" = "0 2 0 0 0 0 0 124 0 0" ] \
  || { echo "=== CHECKS FAILED: reprogram the FPGA"; exit 1; }
ARGS="--data $RL/data --loadable $RL/rl_lenet_ip2.nvdla --n 100"
# RL_BIN: an rl_board pushed into RAM (e.g. /root/rl_board_fix with the layout fix)
BIN=${RL_BIN:-./rl_board}
echo "rl_board binary: $BIN $(md5sum $BIN | cut -c1-12)"
run() { name=$1; shift; echo "=== $name start"; $BIN "$@" $ARGS --csv results/board_$name.csv; echo "=== $name rc=$?"; }
case "$1" in
serial)
  run serial_delta                --mode serial --handoff delta
  run serial_reasonlink           --mode serial --handoff reasonlink
  run serial_delta_resident       --mode serial --handoff delta --resident
  run serial_reasonlink_resident  --mode serial --handoff reasonlink --resident
  run serial_faithful_trace       --mode serial --clear full --xfer allocated --nn trace ;;
serial_reasonlink_resident) run serial_reasonlink_resident --mode serial --handoff reasonlink --resident ;;
double_lean)                run double_lean                --mode double --clear lean --xfer used ;;
double_delta)               run double_delta               --mode double --handoff delta ;;
double_reasonlink)          run double_reasonlink          --mode double --handoff reasonlink ;;
double_delta_resident)      run double_delta_resident      --mode double --handoff delta --resident ;;
double_reasonlink_resident) run double_reasonlink_resident --mode double --handoff reasonlink --resident ;;
*) echo "unknown batch $1"; exit 1 ;;
esac
echo "=== BATCH DONE $1"
