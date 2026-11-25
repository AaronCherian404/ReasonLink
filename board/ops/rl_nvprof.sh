#!/bin/sh
# On the board: NVDLA driver profile (2026-11-24). ESP builds opendla.ko with
# VERBOSE=1 (about 470 printk calls per LeNet task); every earlier board run used
# it. A: the stock driver; B: the same driver built with VERBOSE=0
# (/root/opendla_quiet.ko). Same rl_board binary, puzzles and loadable in both.
# Usage: sh rl_nvprof.sh   (NVPROF_N puzzles per run, default 20)
# Log: /root/rl_nvprof.log; CSVs results/nvprof_*.csv; /root/nvprof_*_dmesg.txt
RL=/root/CU_SATACCEL/rl
cd $RL
BIN=${RL_BIN:-./rl_board}
N=${NVPROF_N:-20}
ARGS="--data $RL/data --loadable $RL/rl_lenet_ip2.nvdla --n $N"
dmesg -n 1
kill $(pidof klogd) 2>/dev/null
check() {
  nv=$(cd /root/NV_NVDLA && ./nvdla_runtime --loadable lenet_mnist.nvdla --image seven.pgm --rawdump > /dev/null && cat output.dimg)
  echo "NVDLA seven.pgm: $nv (ESP's published output: 0 2 0 0 0 0 0 124 0 0)"
  [ "$(echo $nv)" = "0 2 0 0 0 0 0 124 0 0" ]
}
sa=$(/root/CU_SATACCEL/sataccel_run cnf "$RL/data/base.cnf" 1); echo "$sa"
echo "$sa" | grep -q "^1/1 correct" && check || { echo "=== CHECKS FAILED: reprogram the FPGA"; exit 1; }
echo "rl_board binary: $BIN $(md5sum $BIN | cut -c1-12)"
echo "stock driver: $(md5sum /opt/drivers/opendla.ko | cut -c1-12)  quiet driver: $(md5sum /root/opendla_quiet.ko | cut -c1-12)"
run() { name=$1; shift; echo "=== $name start up=$(cut -d' ' -f1 /proc/uptime)"; $BIN "$@" $ARGS --csv results/nvprof_$name.csv; echo "=== $name rc=$? up=$(cut -d' ' -f1 /proc/uptime)"; }

# A: stock driver (VERBOSE=1). One isolated task first, for a clean per-event timeline.
dmesg -c > /dev/null
check > /dev/null
dmesg > /root/nvprof_verbose_one_task_dmesg.txt
run verbose_delta --mode serial --handoff delta

# B: quiet driver (VERBOSE=0)
rmmod opendla && insmod /root/opendla_quiet.ko || { echo "=== MODULE SWAP FAILED"; exit 1; }
check || { echo "=== QUIET DRIVER CHECK FAILED"; exit 1; }
run quiet_delta --mode serial --handoff delta
run quiet_reasonlink --mode serial --handoff reasonlink
run quiet_faithful --mode serial --clear full --xfer allocated
dmesg > /root/nvprof_quiet_dmesg.txt
echo "=== BATCH DONE nvprof"
