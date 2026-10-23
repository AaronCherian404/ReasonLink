#!/usr/bin/env bash
# Copy SAT-Accel's pinned kernel sources to build/src and apply our patches
# (patches/*.patch, in order). The submodule itself stays untouched.
# Usage (from sataccel_hw/): bash prepare_src.sh
set -euo pipefail
cd "$(dirname "$0")"
UPSTREAM=../third_party/FPGA25_SAT_Accel/src
rm -rf build/src && mkdir -p build/src
for f in "$UPSTREAM"/*.cpp "$UPSTREAM"/*.h; do
  case "$(basename "$f")" in host.cpp|xcl2.cpp|xcl2.hpp) continue ;; esac   # host side, not kernels
  tr -d '\r' < "$f" > "build/src/$(basename "$f")"
done
shopt -s nullglob
for p in patches/*.patch; do
  echo "applying $p"
  patch -s -d build/src -p1 < "$p"
done
echo "build/src ready ($(ls build/src | wc -l) files)"
