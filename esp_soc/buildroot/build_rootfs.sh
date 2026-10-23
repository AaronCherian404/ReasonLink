#!/usr/bin/env bash
# Build ESP's Linux root file system (buildroot) into <esp>/soft/ariane/sysroot:
# the "Populating root file system w/ buildroot" step of ESP's
# utils/toolchain/build_riscv_toolchain.sh, run non-interactively, adjusted
# to the installed toolchain (GCC 9.2, kernel headers 5.0):
#   - ESP's riscv_buildroot_defconfig declares GCC 8 / headers 4.13, and
#     buildroot checks these against the real toolchain;
#   - MD5 passwords were removed from buildroot after 2018: SHA-256;
#   - ESP pins buildroot d6fa6a4 (Sept. 2018), which knows GCC <= 8 and
#     headers <= 4.18, so we use the 2020.02.12 LTS release instead.
# Usage (torino): bash esp_soc/buildroot/build_rootfs.sh [esp_root]   (~30-60 min)
set -euo pipefail
ESP_ROOT="${1:-$HOME/esp}"
export RISCV="${RISCV:?RISCV must point at the RISC-V toolchain}"
export ARCH=riscv64
export SCRIPT_PATH="$ESP_ROOT/utils/toolchain"
export SYSROOT="$ESP_ROOT/soft/ariane/sysroot"
WORK="${WORK:-$HOME/buildroot_esp}"
BUILDROOT_REF="${BUILDROOT_REF:-2020.02.12}"   # ESP pins d6fa6a45e (2018), too old for this toolchain

# ESP's defconfig, adjusted to this toolchain
mkdir -p "$WORK"
sed -e 's/^BR2_TOOLCHAIN_EXTERNAL_GCC_8=y/BR2_TOOLCHAIN_EXTERNAL_GCC_9=y/' \
    -e 's/^BR2_TOOLCHAIN_EXTERNAL_HEADERS_4_13=y/BR2_TOOLCHAIN_EXTERNAL_HEADERS_5_0=y/' \
    -e '/^BR2_TARGET_GENERIC_PASSWD_MD5=y/d' \
    -e 's/^BR2_TARGET_GENERIC_PASSWD_METHOD="md5"/BR2_TARGET_GENERIC_PASSWD_METHOD="sha-256"/' \
    "$SCRIPT_PATH/riscv_buildroot_defconfig" > "$WORK/riscv_buildroot_defconfig"

# Reset the sysroot overlay to its committed content
(cd "$ESP_ROOT" && rm -rf "$SYSROOT"/* && git checkout -- soft/ariane/sysroot)

cd "$WORK"
# Only the pinned commit, from the GitHub mirror (a full clone from
# git.buildroot.net failed on torino with "bad pack header")
if [ ! -d buildroot/.git ]; then
  rm -rf buildroot && git init -q buildroot
  git -C buildroot remote add origin https://github.com/buildroot/buildroot.git
fi
cd buildroot
git fetch -q --depth 1 origin tag "$BUILDROOT_REF"
git reset -q --hard "$BUILDROOT_REF"
git submodule update --init --recursive
make distclean
make defconfig BR2_DEFCONFIG="$WORK/riscv_buildroot_defconfig"
make -j "$(nproc)"

rm -f output/target/THIS_IS_NOT_YOUR_ROOT_FILESYSTEM
cp -r output/target/* "$SYSROOT"/
[ -e "$SYSROOT/init" ] || install -m 0755 fs/cpio/init "$SYSROOT/init"
(cd "$ESP_ROOT" && git checkout HEAD -- soft/ariane/sysroot/etc/init.d/S65drivers)
echo "root file system ready in $SYSROOT ($(du -sh "$SYSROOT" | cut -f1))"
