#!/usr/bin/env bash
# Development helper: copy files from the torino clone into this checkout.
# torino's login prints to stdout, so the archive travels as base64 between
# markers. Usage (from sataccel_hw/): bash fetch_torino.sh <path relative to ReasonLink/>...
set -euo pipefail
cd "$(dirname "$0")/.."
ssh -o BatchMode=yes torino.cs.columbia.edu \
  "cd ~/ReasonLink && echo RL_BEGIN && tar czf - $* | base64 -w0 && echo && echo RL_END" 2>/dev/null |
  sed -n '/^RL_BEGIN$/,/^RL_END$/p' | sed '1d;$d' | base64 -d | tar xzvf -
