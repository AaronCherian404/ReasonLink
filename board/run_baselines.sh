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
irqs() { awk '/sataccel/ {print $2}' /proc/interrupts; }
i0=$(irqs)
/root/CU_SATACCEL/sataccel_run cnf "$RL/data/base.cnf" 1
# The completion interrupt must arrive, or every solve waits out the timeout.
# ESP's socket blocks it if an earlier interrupt was never acknowledged (e.g.
# an old bare-metal test that enabled it, then a reboot): then poll only, and
# reprogram the FPGA before the next run.
if [ "$(irqs)" = "$i0" ]; then
  echo "WARNING: no SAT-Accel interrupt (reprogram the FPGA to clear it); polling for completion"
  export SATACCEL_WAIT_MS=0
else
  echo "SAT-Accel completion interrupt OK ($i0 -> $(irqs))"
fi
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
# ReasonLink (phase B hardware), only if this bitstream's SAT-Accel tile has it
for mode in serial double; do
  ./rl_board --mode $mode --handoff reasonlink $ARGS --csv results/board_${mode}_reasonlink.csv \
    || { echo "(no ReasonLink in this bitstream: skipped)"; rm -f results/board_${mode}_reasonlink.csv; break; }
done
# ReasonLink Milestone 2 (bitstreams built with patches/0002): the base
# formula resident on chip after the first solve, for the CPU delta and for
# ReasonLink. On a bitstream without patch 0002 the flag writes an unused
# register: SAT-Accel copies everything in, as in the delta runs
for mode in serial double; do
  ./rl_board --mode $mode --handoff delta --resident $ARGS --csv results/board_${mode}_delta_resident.csv
done
for mode in serial double; do
  ./rl_board --mode $mode --handoff reasonlink --resident $ARGS --csv results/board_${mode}_reasonlink_resident.csv \
    || { echo "(no ReasonLink in this bitstream: skipped)"; rm -f results/board_${mode}_reasonlink_resident.csv; break; }
done
# NVDLA isolated: the same pipeline replaying the emulator's logits
./rl_board --mode serial --clear full --xfer allocated --nn trace $ARGS --csv results/board_serial_faithful_trace.csv
ls -l results
