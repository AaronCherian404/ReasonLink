#!/usr/bin/env bash
# Development helper: copy sataccel_hw/ sources (not build/) to the torino
# clone without a commit, for quick iteration. Committed work still reaches
# torino through GitHub.
set -euo pipefail
cd "$(dirname "$0")/.."
tar --exclude='sataccel_hw/build' -czf - sataccel_hw | \
  ssh -o BatchMode=yes torino.cs.columbia.edu 'cd ~/ReasonLink && tar xzf - && echo synced' 2>&1 | grep -v '^Exported\|riscv_env\|Synopsys'
