#!/usr/bin/env bash
# Development helper: copy uncommitted sources (default: sataccel_hw/ and
# esp_soc/, without build outputs) to the torino clone for quick iteration.
# Committed work still reaches torino through GitHub.
# Usage: bash sataccel_hw/sync_torino.sh [dir ...]   (dirs relative to ReasonLink/)
set -euo pipefail
cd "$(dirname "$0")/.."
DIRS=("$@"); [ ${#DIRS[@]} -eq 0 ] && DIRS=(sataccel_hw esp_soc)
tar --exclude='*/build' --exclude='esp_soc/accelerator/CU_SATACCEL/netlist' -czf - "${DIRS[@]}" | \
  ssh -o BatchMode=yes torino.cs.columbia.edu 'cd ~/ReasonLink && tar xzf - --no-same-permissions && echo synced' 2>&1 | grep -v '^Exported\|riscv_env\|Synopsys'
