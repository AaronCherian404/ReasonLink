#!/bin/bash
# torino: reprogram the FPGA with the ReasonLink backup, boot Linux, push the
# quiet NVDLA driver, rl_board_fix and rl_nvprof.sh, start the profile detached.
# Usage (RL_PW in the env): nvprof_run.sh     Log: ~/m2/boot_retry.log
# Results: fetch after ~10 min with board_fetch (no SSH to the board meanwhile).
set -u
B=${RL_BACKUP:-$HOME/m2/backup_reasonlink_20261105}
Q=${NVPROF_KO:-$HOME/m2/nvdla_quiet/kmd/port/linux/opendla.ko}
L=~/m2/boot_retry.log
: > $L
# Lab FPGA settings (FPGA_HOST, UART_*, ESPLINK_*, ...): sourced before any make/esplink/vivado
FPGA_SCRIPT=${FPGA_SCRIPT:-$HOME/esp/socs/reasonlink-vcu118/fpga-script.sh}
. "$FPGA_SCRIPT" || { echo "$(date +%T) cannot source $FPGA_SCRIPT" >> $L; exit 1; }
tmux has-session -t rlb 2>/dev/null || tmux new-session -d -s rlb -n uart \
  "while true; do socat -u TCP:$UART_IP:$UART_PORT - >> ~/m2/uart_live.log; sleep 2; done"
echo "$(date +%T) programming $B/top_rl.bit ($(md5sum < $B/top_rl.bit | cut -c1-8))" >> $L
(cd ~/m2 && vivado -mode batch -nojournal -log ~/m2/program_rl_auto.log -source $B/program.tcl \
   -tclargs $FPGA_HOST $XIL_HW_SERVER_PORT xcvu9p $B/top_rl.bit > /dev/null 2>&1)
grep -q "End of startup status: HIGH" ~/m2/program_rl_auto.log || { echo "$(date +%T) PROGRAM_FAILED" >> $L; exit 1; }
sleep 5
RL_BACKUP=$B ~/m2/boot_retry.sh || exit 1
sleep 15
timeout 60  ~/m2/board_put.sh ~/m2/rl_nvprof.sh /root/rl_nvprof.sh >> $L 2>&1
timeout 200 ~/m2/board_put_bin.sh $B/rl_board_fix /root/rl_board_fix >> $L 2>&1
timeout 300 ~/m2/board_put_bin.sh $Q /root/opendla_quiet.ko >> $L 2>&1
timeout 60 ~/m2/bssh.sh "md5sum /root/rl_board_fix /root/opendla_quiet.ko; RL_BIN=/root/rl_board_fix nohup sh /root/rl_nvprof.sh > /root/rl_nvprof.log 2>&1 < /dev/null & sleep 2; echo started" >> $L 2>&1
echo "$(date +%T) BATCH_STARTED nvprof" >> $L
