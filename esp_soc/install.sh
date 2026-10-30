#!/usr/bin/env bash
# Hook ReasonLink's ESP pieces into an ESP checkout (default ~/esp, tag
# 2026.1.0) without copying them: symlinks plus one small edit.
#   accelerators/third-party/CU_SATACCEL  -> esp_soc/accelerator/CU_SATACCEL
#   soft/common/apps/baremetal/sataccel   -> esp_soc/apps/baremetal/sataccel
#   tools/socgen/thirdparty.py            + compatible string / IRQ type for CU_SATACCEL
#   socs/reasonlink-vcu118                 copy of socs/xilinx-vcu118-xcvu9p whose
#                                          Makefile uses esp_soc/socs/reasonlink-vcu118/esp_defconfig,
#                                          plus vsim.tcl (batch `make sim`) and a
#                                          vivado-syn-rl / vivado-impl-rl targets
#                                          (link SAT-Accel's .dcp in implementation)
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
ln -sfn "$HERE/socs/reasonlink-vcu118/vsim.tcl" "$SOC/vsim.tcl"
if ! grep -q '^ESP_DEFCONFIG' "$SOC/Makefile"; then
  sed -i "/^DESIGN   = esp-\$(BOARD)/a ESP_DEFCONFIG = $HERE/socs/reasonlink-vcu118/esp_defconfig" "$SOC/Makefile"
fi
rm -f "$SOC/add_sataccel_dcp.tcl"                         # older flow
ln -sfn "$HERE/socs/reasonlink-vcu118/sataccel_impl.tcl" "$SOC/sataccel_impl.tcl"
sed -i '/^### ReasonLink: SAT-Accel/,$d' "$SOC/Makefile"     # replace an older version
cat >> "$SOC/Makefile" <<'MK'
### ReasonLink: SAT-Accel's synthesized checkpoints ###
# The SAT-Accel tile's simulation netlist holds encrypted Xilinx IP, which
# Vivado will not re-synthesize. ESP's Vivado flow synthesizes a black-box
# stub instead (CU_SATACCEL/out/sataccel_top_sel.v), and sataccel_impl.tcl
# gives each implementation run an opt_design pre-hook that fills it with a
# checkpoint: impl_1 the baseline SAT-Accel (top.bit), impl_rl SAT-Accel +
# ReasonLink (top_rl.bit, if CU_SATACCEL/out/sataccel_top_rl.dcp exists).
#   vivado-syn-rl    new project, ESP's synthesis + impl_1 (use instead of vivado-syn)
#   vivado-impl-rl   implement IMPL_RUNS on the existing synthesis
SATACCEL_OUT = $(ESP_ROOT)/accelerators/third-party/CU_SATACCEL/out
SATACCEL_IMPL = cd vivado && vivado -mode batch -nojournal -log sataccel_impl.log \
	-source $(DESIGN_PATH)/sataccel_impl.tcl -tclargs $(DESIGN).xpr \
	$(SATACCEL_OUT)/sataccel_top.dcp $(SATACCEL_OUT)/sataccel_top_rl.dcp
IMPL_RUNS ?= impl_1 impl_rl
vivado-syn-rl:
	rm -rf vivado
	$(MAKE) vivado-setup
	$(SATACCEL_IMPL) setup
	yes n | $(MAKE) vivado-syn
vivado-impl-rl:
	$(SATACCEL_IMPL) run $(IMPL_RUNS)
	@for r in $(IMPL_RUNS); do b=vivado/$(DESIGN).runs/$$r/$(TOP).bit; \
	  n=$(TOP).bit; [ $$r = impl_rl ] && n=$(TOP)_rl.bit; \
	  if test -r $$b; then ln -sfn $$b $$n; echo "$$n -> $$b"; fi; done
.PHONY: vivado-syn-rl vivado-impl-rl
MK
echo "installed into $ESP:"
ls -l "$ESP/accelerators/third-party/CU_SATACCEL" "$ESP/soft/common/apps/baremetal/sataccel" | sed 's/^/  /'
grep -n "cu_sataccel" "$TP" | sed 's/^/  /'
grep -n "^ESP_DEFCONFIG" "$SOC/Makefile" | sed 's/^/  /'
