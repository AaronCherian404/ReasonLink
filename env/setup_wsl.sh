#!/usr/bin/env bash
# Local (WSL Ubuntu / Linux) environment for ReasonLink.
# Creates the Python venv outside the repo (venvs on /mnt/c are slow) and
# verifies the imports used by the workload scripts.
#
# Usage: bash env/setup_wsl.sh            (venv at ~/venvs/reasonlink)
#        VENV=/path/to/venv bash env/setup_wsl.sh
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
VENV="${VENV:-$HOME/venvs/reasonlink}"

if ! python3 -c 'import ensurepip' 2>/dev/null; then
    echo "[setup] installing python3-venv and build tools (needs sudo)"
    sudo apt-get update -qq
    sudo apt-get install -y -qq python3-venv python3-dev build-essential
fi

if [ ! -x "$VENV/bin/python" ]; then
    echo "[setup] creating venv at $VENV"
    python3 -m venv "$VENV"
fi

"$VENV/bin/pip" install -q --upgrade pip
"$VENV/bin/pip" install -q -r "$REPO/env/requirements.txt"
"$VENV/bin/pip" freeze > "$REPO/env/requirements.lock.txt"

"$VENV/bin/python" - <<'EOF'
import sys, numpy, torch, torchvision, pysat
from pysat.solvers import Solver
with Solver(name="m22", bootstrap_with=[[1, 2], [-1]]) as s:
    assert s.solve() and s.get_model() == [-1, 2]
print("python", sys.version.split()[0])
print("numpy", numpy.__version__, "| torch", torch.__version__,
      "| torchvision", torchvision.__version__, "| python-sat", pysat.__version__)
print("[setup] OK")
EOF
