#!/usr/bin/env bash
# Inventory of the Columbia server: OS, resources, and the CAD tools ESP and
# SAT-Accel need. Read-only; prints a Markdown block to paste into PROGRESS.md.
#
# Usage (from a local shell): ssh <host> 'bash -s' < env/server_probe.sh
set -uo pipefail

ver() {  # ver <label> <command...>: first line of output, or "not found"
    local label="$1"; shift
    local out
    if out="$("$@" 2>&1 | grep -v '^\s*$' | head -1)" && [ -n "$out" ]; then
        printf -- '- %s: `%s`\n' "$label" "$out"
    else
        printf -- '- %s: not found\n' "$label"
    fi
}

echo "### Server inventory ($(date -u +%Y-%m-%dT%H:%MZ))"
echo "- Host: \`$(hostname -f 2>/dev/null || hostname)\`"
echo "- OS: \`$(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME")\`, kernel \`$(uname -r)\`"
echo "- glibc: \`$(ldd --version 2>&1 | head -1)\`"
echo "- CPUs: $(nproc); RAM: $(free -g | awk '/Mem:/{print $2}') GB"
echo "- Disk (home): \`$(df -h "$HOME" | awk 'NR==2{print $4" free of "$2}')\`"
echo "- Module system: $(command -v module >/dev/null 2>&1 && echo yes || echo no)"
echo
echo "#### Tools on PATH"
ver "gcc" gcc --version
ver "git" git --version
ver "python3" python3 --version
ver "vivado" vivado -version
ver "vitis_hls" vitis_hls -version
ver "v++ (Vitis)" v++ --version
ver "xrt (xbutil)" xbutil --version
ver "vsim (ModelSim/Questa)" vsim -version
ver "xrun (Xcelium)" xrun -version
ver "riscv64-unknown-elf-gcc" riscv64-unknown-elf-gcc --version
ver "riscv64-unknown-linux-gnu-gcc" riscv64-unknown-linux-gnu-gcc --version
ver "spike" spike --help
ver "qemu-riscv64" qemu-riscv64 --version
echo
echo "#### Top-level of /opt and /tools"
for d in /opt /tools; do
    [ -d "$d" ] && echo "- \`$d\`: $(ls "$d" 2>/dev/null | tr '\n' ' ')"
done
echo
echo "#### Install locations found under /opt, /tools, /usr/local"
for d in /opt /tools /usr/local /eda /cad; do
    [ -d "$d" ] || continue
    find "$d" -maxdepth 3 -type d \( -iname 'Vivado*' -o -iname 'Vitis*' -o -iname 'xrt' \
        -o -iname 'modelsim*' -o -iname 'questa*' -o -iname 'XCELIUM*' -o -iname 'cadence' \
        -o -iname 'mentor' -o -iname 'siemens' -o -iname 'Xilinx' \) 2>/dev/null | sed 's/^/- `/; s/$/`/'
done
echo
echo "#### License environment"
for v in XILINXD_LICENSE_FILE LM_LICENSE_FILE MGLS_LICENSE_FILE CDS_LIC_FILE SALT_LICENSE_SERVER; do
    printf -- '- %s=%s\n' "$v" "${!v:-<unset>}"
done
