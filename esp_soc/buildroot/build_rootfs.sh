#!/usr/bin/env bash
# Build ESP's Linux root file system (buildroot) into <esp>/soft/ariane/sysroot:
# the "Populating root file system w/ buildroot" step of ESP's
# utils/toolchain/build_riscv_toolchain.sh, run non-interactively, with ESP's
# riscv_buildroot_defconfig adjusted to the installed toolchain (GCC 9.2,
# kernel headers 5.0; ESP's file declares GCC 8 / headers 4.13, which
# buildroot rejects for this toolchain).
# Usage (torino): bash esp_soc/buildroot/build_rootfs.sh [esp_root]   (~30-60 min)
set -euo pipefail
ESP_ROOT="${1:-$HOME/esp}"
export RISCV="${RISCV:?RISCV must point at the RISC-V toolchain}"
export ARCH=riscv64
export SCRIPT_PATH="$ESP_ROOT/utils/toolchain"
export SYSROOT="$ESP_ROOT/soft/ariane/sysroot"
WORK="${WORK:-$HOME/buildroot_esp}"
BUILDROOT_SHA=d6fa6a45e196665d6607b522f290b1451b949c2c   # build_riscv_toolchain.sh BUILDROOT_SHA_DEFAULT

# ESP's defconfig, adjusted to this toolchain
mkdir -p "$WORK"
sed -e 's/^BR2_TOOLCHAIN_EXTERNAL_GCC_8=y/BR2_TOOLCHAIN_EXTERNAL_GCC_9=y/' \
    -e 's/^BR2_TOOLCHAIN_EXTERNAL_HEADERS_4_13=y/BR2_TOOLCHAIN_EXTERNAL_HEADERS_5_0=y/' \
    "$SCRIPT_PATH/riscv_buildroot_defconfig" > "$WORK/riscv_buildroot_defconfig"

# Reset the sysroot overlay to its committed content
(cd "$ESP_ROOT" && rm -rf "$SYSROOT"/* && git checkout -- soft/ariane/sysroot)

cd "$WORK"
[ -d buildroot ] || git clone https://git.buildroot.net/buildroot
cd buildroot
git reset -q --hard "$BUILDROOT_SHA"
git submodule update --init --recursive
make distclean
make defconfig BR2_DEFCONFIG="$WORK/riscv_buildroot_defconfig"
make -j "$(nproc)"

rm -f output/target/THIS_IS_NOT_YOUR_ROOT_FILESYSTEM
cp -r output/target/* "$SYSROOT"/
[ -e "$SYSROOT/init" ] || install -m 0755 fs/cpio/init "$SYSROOT/init"
(cd "$ESP_ROOT" && git checkout HEAD -- soft/ariane/sysroot/etc/init.d/S65drivers)
echo "root file system ready in $SYSROOT ($(du -sh "$SYSROOT" | cut -f1))"
