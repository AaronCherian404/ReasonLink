#!/usr/bin/env bash
# Hook ReasonLink's ESP pieces into an ESP checkout (default ~/esp, tag
# 2026.1.0) without copying them: symlinks plus one small edit.
#   accelerators/third-party/CU_SATACCEL  -> esp_soc/accelerator/CU_SATACCEL
#   soft/common/apps/baremetal/sataccel   -> esp_soc/apps/baremetal/sataccel
#   tools/socgen/thirdparty.py            + compatible string / IRQ type for CU_SATACCEL
#   socs/reasonlink-vcu118                 copy of socs/xilinx-vcu118-xcvu9p whose
#                                          Makefile uses esp_soc/socs/reasonlink-vcu118/esp_defconfig
# Idempotent. Usage: bash esp_soc/install.sh [esp_root]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd -P)"
ESP="${1:-$HOME/esp}"
[ -d "$ESP/socs/xilinx-vcu118-xcvu9p" ] || { echo "not an ESP checkout: $ESP"; exit 1; }

ln -sfn "$HERE/accelerator/CU_SATACCEL" "$ESP/accelerators/third-party/CU_SATACCEL"
ln -sfn "$HERE/apps/baremetal/sataccel" "$ESP/soft/common/apps/baremetal/sataccel"

TP="$ESP/tools/socgen/thirdparty.py"
if ! grep -q '"cu_sataccel"' "$TP"; then
  sed -i '/^THIRDPARTY_IRQ_TYPE\["nv_nvdla"\]/a\
\
# SAT-Accel (ReasonLink): level interrupt (solver ap_done, cleared by the driver)\
THIRDPARTY_COMPATIBLE["cu_sataccel"] = "sataccel"\
THIRDPARTY_IRQ_TYPE["cu_sataccel"] = "1"' "$TP"
fi

SOC="$ESP/socs/reasonlink-vcu118"
if [ ! -d "$SOC" ]; then
  cp -r "$ESP/socs/xilinx-vcu118-xcvu9p" "$SOC"
fi
if ! grep -q '^ESP_DEFCONFIG' "$SOC/Makefile"; then
  sed -i "/^DESIGN   = esp-\$(BOARD)/a ESP_DEFCONFIG = $HERE/socs/reasonlink-vcu118/esp_defconfig" "$SOC/Makefile"
fi
echo "installed into $ESP:"
ls -l "$ESP/accelerators/third-party/CU_SATACCEL" "$ESP/soft/common/apps/baremetal/sataccel" | sed 's/^/  /'
grep -n "cu_sataccel" "$TP" | sed 's/^/  /'
grep -n "^ESP_DEFCONFIG" "$SOC/Makefile" | sed 's/^/  /'
