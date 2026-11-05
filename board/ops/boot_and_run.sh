#!/bin/bash
# Reprogram the FPGA (full reset: esplink --reset leaves a hung accelerator hung),
# boot Linux, copy rl_batch.sh onto the board, start one batch.
# Usage (RL_PW in the env): boot_and_run.sh <batch>     Log: ~/m2/boot_retry.log
L=~/m2/boot_retry.log
: > $L
V=~/esp/socs/reasonlink-vcu118/vivado
echo "$(date +%T) programming top_rl.bit" >> $L
(cd $V && vivado -mode batch -nojournal -log ~/m2/program_rl_auto.log -source program.tcl \
   -tclargs espdev 3132 xcvu9p $V/esp-xilinx-vcu118-xcvu9p.runs/impl_rl/top.bit > /dev/null 2>&1)
grep -q "End of startup status: HIGH" ~/m2/program_rl_auto.log || { echo "$(date +%T) PROGRAM_FAILED" >> $L; exit 1; }
sleep 5   # DDR calibration, as ESP's fpga-program
~/m2/boot_retry.sh || exit 1
sleep 15  # let dropbear and the drivers settle
timeout 60 ~/m2/board_put.sh ~/m2/rl_batch.sh /root/rl_batch.sh >> $L 2>&1
# rl_board with the ReasonLink layout fix (not in the initramfs yet): into RAM
timeout 200 ~/m2/board_put_bin.sh ~/m2/rldbg/rl_board_fix /root/rl_board_fix >> $L 2>&1
timeout 60 ~/m2/bssh.sh "RL_BIN=/root/rl_board_fix nohup sh /root/rl_batch.sh $1 > /root/rl_$1.log 2>&1 < /dev/null & sleep 2; echo started" >> $L 2>&1
echo "$(date +%T) BATCH_STARTED $1" >> $L
