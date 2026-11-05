#!/bin/bash
# Copy the board run results off every 5 min (RL_PW in the env, never stored).
# A hung SSH is stopped by its own PID (children of this fetch only).
# The board has no tar/gzip: each CSV is cat'ed between marker lines and split here.
D=~/m2/board_fetch
mkdir -p $D/results
fetch() {  # $1 = remote command, $2 = output file
  ~/m2/bssh.sh "$1" > "$2.part" 2>/dev/null & bp=$!
  for t in $(seq 1 90); do kill -0 $bp 2>/dev/null || break; sleep 1; done
  if kill -0 $bp 2>/dev/null; then
    for c in $(ps -o pid= --ppid $bp); do kill $c; done; kill $bp
    echo "$(date +%T) TIMEOUT $2" >> $D/fetch.log; return 1
  fi
  wait $bp; rc=$?
  echo "$(date +%T) rc=$rc $2 $(stat -c %s "$2.part" 2>/dev/null)" >> $D/fetch.log
  [ $rc = 0 ] && [ -s "$2.part" ] && mv "$2.part" "$2"
}
REMOTE_CSV='cd /root/CU_SATACCEL/rl/results && for f in *.csv; do [ -f "$f" ] && echo "=== $f" && cat "$f"; done; echo "=== END"'
while true; do
  if fetch "$REMOTE_CSV" $D/results_latest.txt && grep -q '^=== END' $D/results_latest.txt; then
    awk -v d=$D/results '/^=== /{f = d "/" $2; next} f {print > f}' $D/results_latest.txt
    rm -f $D/results/END
  fi
  fetch 'for f in /root/rl_*.log; do echo "### $f"; cat "$f"; done; echo; echo PID:$(pidof rl_board)' $D/rl_logs.txt
  sleep 300
done
