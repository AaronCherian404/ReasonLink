#!/bin/sh
# Board run (Linux on the reasonlink-vcu118 SoC): load the drivers, check
# both accelerators, then run the serial and double-buffered baselines in the
# faithful and lean configurations, as P1 did on the host. Results go to
# /root/CU_SATACCEL/rl/results (copy them off the board, e.g. scp over ESP's Ethernet).
#
# Usage (on the board): sh /root/CU_SATACCEL/rl/run_baselines.sh [num_puzzles]   (default: all 1000)
set -e
RL=/root/CU_SATACCEL/rl
N=${1:-0}
cd "$RL"

# Drivers: ESP's init loads its own (S64esp); NVDLA and SAT-Accel are third-party
lsmod | grep -q opendla || insmod /opt/drivers/opendla.ko
lsmod | grep -q sataccel || insmod /opt/drivers/sataccel.ko
dmesg | grep -i sataccel | tail -3

# Bring-up checks: SAT-Accel alone (the base Sudoku formula, no givens: SAT),
# then NVDLA on ESP's demo image if present
/root/CU_SATACCEL/sataccel_run cnf "$RL/data/base.cnf" 1
if [ -f /root/NV_NVDLA/seven.pgm ]; then
  (cd /root/NV_NVDLA && ./nvdla_runtime --loadable lenet_mnist.nvdla --image seven.pgm --rawdump > /dev/null && cat output.dimg)
  echo "(ESP's published NVDLA output for seven.pgm: 0 2 0 0 0 0 0 124 0 0)"
fi

ARGS="--data $RL/data --loadable $RL/rl_lenet_ip2.nvdla"
[ "$N" -gt 0 ] && ARGS="$ARGS --n $N"
for mode in serial double; do
  ./rl_board --mode $mode --clear full --xfer allocated $ARGS --csv results/board_${mode}_faithful.csv
  ./rl_board --mode $mode --clear lean --xfer used      $ARGS --csv results/board_${mode}_lean.csv
done
# Delta transfers as a CPU-only optimization (ReasonLink mechanism iii, the
# proposal's ablation): givens as fixed decisions on a resident base image
for mode in serial double; do
  ./rl_board --mode $mode --handoff delta $ARGS --csv results/board_${mode}_delta.csv
done
# NVDLA isolated: the same pipeline replaying the emulator's logits
./rl_board --mode serial --clear full --xfer allocated --nn trace $ARGS --csv results/board_serial_faithful_trace.csv
ls -l results
