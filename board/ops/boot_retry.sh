#!/bin/bash
# Boot Linux on the board: retry fpga-run-linux until the EDCL load succeeds,
# then make sure the final reset took (boot loader banner within 20 s; if not,
# one more esplink --reset), then wait for the login prompt. Log: ~/m2/boot_retry.log
# RL_BACKUP=<dir>: boot from a backup (esplink, prom.bin, linux.bin) instead of the
# SoC folder, e.g. after the folder was reconfigured for another project.
L=~/m2/boot_retry.log; U=~/m2/uart_live.log
B=${RL_BACKUP:-}
if [ -n "$B" ]; then
  E=$B/socgen_esp/esplink
  load() { $E --reset && $E --brom -i $B/prom.bin && $E --dram -i $B/linux.bin && $E --reset; }
else
  cd ~/esp/socs/reasonlink-vcu118 || exit 1
  E=./socgen/esp/esplink
  load() { make fpga-run-linux; }
fi
say() { echo "$(date +%T) $*" >> $L; }
seen_after() { tail -c +$(( $1 + 1 )) $U | grep -a -q -E "$2"; }
for n in 1 2 3 4 5 6 7 8; do
  say "attempt $n${B:+ (from $B)}"
  load > ~/m2/fpga_run_linux_try$n.log 2>&1 && { ok=1; break; }
  say "  load failed: $(tr '\r' '\n' < ~/m2/fpga_run_linux_try$n.log | grep -E 'Error' | tail -1)"
  sleep 10
done
[ -n "$ok" ] || { say BOOT_FAILED; exit 1; }
for r in 1 2 3; do
  off=$(wc -c < $U)
  [ $r -gt 1 ] && { say "  no boot banner: extra esplink --reset"; $E --reset > /dev/null 2>&1; }
  for t in $(seq 1 10); do seen_after $off "Linux version" && break 2; sleep 2; done
done
for t in $(seq 1 60); do
  seen_after $off "Kernel panic" && { say BOOT_PANIC; exit 1; }
  seen_after $off "login:" && { say BOOT_LOGIN; exit 0; }
  sleep 4
done
say BOOT_NO_LOGIN; exit 1
