#!/usr/bin/env bash
# Compile LeNet loadables for NVDLA nv_small with ESP's prebuilt nvdla_compiler.
# Run on a machine with the ESP NV_NVDLA/sw submodule (torino: ~/esp).
#
#   1. Recompile ESP's demo model and check it reproduces ESP's shipped
#      lenet_mnist.nvdla byte for byte (validates compiler + flags).
#   2. Compile our retrained model (models/rl_lenet) -> build/nvdla/rl_lenet.nvdla
#
# Flags: --quantizationMode per-filter --informat nchw are what ESP used (only
# this combination reproduces their md5). The compiler fails to open
# --calibtable given an absolute path (rapidjson assertion), so inputs are
# copied into the working directory and passed as bare file names.
#
# Usage: ESP_ROOT=~/esp bash workload/nvdla_compile.sh
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ESP_ROOT="${ESP_ROOT:-$HOME/esp}"
NVC="$ESP_ROOT/accelerators/third-party/NV_NVDLA/sw/prebuilt/x86-ubuntu"
FLAGS=(--cprecision int8 --configtarget nv_small --quantizationMode per-filter --informat nchw)
ESP_MD5=9831533fd3257eea712a81ec47c95c01

compile() {  # compile <workdir> <prototxt> <caffemodel> <calibtable> <out.nvdla>
    (cd "$1" && rm -f fast-math.nvdla &&
     LD_LIBRARY_PATH="$NVC" "$NVC/nvdla_compiler" --prototxt "$2" --caffemodel "$3" \
         --calibtable "$4" "${FLAGS[@]}" > compile.log 2>&1 &&
     mv fast-math.nvdla "$5")
}

ESP_DIR="$REPO/build/nvdla/esp"
mkdir -p "$ESP_DIR"
for f in lenet_mnist.prototxt lenet_mnist.caffemodel lenet_mnist.json lenet_mnist.nvdla; do
    [ -f "$ESP_DIR/$f" ] || curl -sSfL -o "$ESP_DIR/$f" "https://esp.cs.columbia.edu/docs/thirdparty_acc/$f"
done
compile "$ESP_DIR" lenet_mnist.prototxt lenet_mnist.caffemodel lenet_mnist.json esp_recompiled.nvdla
got=$(md5sum "$ESP_DIR/esp_recompiled.nvdla" | cut -d' ' -f1)
[ "$got" = "$ESP_MD5" ] && echo "[nvdla] ESP loadable reproduced exactly ($got)" \
    || { echo "[nvdla] ESP loadable mismatch: $got != $ESP_MD5"; exit 1; }

RL_DIR="$REPO/build/nvdla"
cp "$REPO"/models/rl_lenet/{lenet_mnist.prototxt,rl_lenet.caffemodel,rl_lenet.json} "$RL_DIR/"
compile "$RL_DIR" lenet_mnist.prototxt rl_lenet.caffemodel rl_lenet.json rl_lenet.nvdla
ls -l "$RL_DIR/rl_lenet.nvdla"
md5sum "$RL_DIR/rl_lenet.nvdla"
