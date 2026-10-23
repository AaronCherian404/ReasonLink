# SAT-Accel as RTL without Vitis (v++/XRT): Vitis HLS + Vivado only.
# Run on torino (Vitis HLS / Vivado 2023.2 on PATH) unless noted.
#
#   make src         build/src: SAT-Accel's kernels + patches/*.patch
#   make hls         Vitis HLS: all 7 kernels -> Vivado IPs (build/hls), ~5 min
#   make regs        copy the HLS register headers to drv/hls_regs (committed)
#   make bd          Vivado block design: kernels + streams + interconnect (build/vivado_bd)
#   make sim-setup   xsim compile/elaborate of tb/tb_sataccel.sv (in build/vivado_bd)
#   make tb_tool     stimulus/check generator (any Linux host)
#   bash tools/run_rtl_tests.sh <out> sudoku:<k> cnf:<file>:<1|0> ...   RTL tests in xsim
#   make check-caps  drv/sataccel_caps.h == patched fpga_solver.h
#   make synth       out-of-context synthesis -> build/vivado_bd/sataccel_top_funcsim.v (for ESP)
# VCU118; 100 MHz (ESP's VCU118 SoC runs at 78 MHz)
PART      ?= xcvu9p-flga2104-2L-e
PERIOD_NS ?= 10
HANDOFF   := ../handoff
CC        ?= gcc
CFLAGS    ?= -O2 -std=gnu11 -Wall -Wextra

all: tb_tool

src:
	bash prepare_src.sh

hls: src
	cd hls && PERIOD_NS=$(PERIOD_NS) PART=$(PART) bash run_hls.sh
	python3 hls/summarize_hls.py build/hls build/hls/summary.md

regs:
	mkdir -p drv/hls_regs
	for k in solver clause_store_handler location_handler restartCalculator timer pqHandler message; do \
	  cp build/hls/$$k/sol/impl/misc/drivers/$${k}_v1_0/src/x$$(echo $$k | tr A-Z a-z)_hw.h drv/hls_regs/; done

# REASONLINK=1: the block design with rtl/reasonlink_core.v, in build/vivado_bd_rl
# (applies to bd, sim-setup and synth)
REASONLINK ?= 0
BD_DIR := build/vivado_bd$(if $(filter 1,$(REASONLINK)),_rl,)

bd:
	mkdir -p $(BD_DIR)
	cd $(BD_DIR) && vivado -mode batch -nojournal -log bd.log \
	  -source $(CURDIR)/vivado/sataccel_bd.tcl -tclargs $(CURDIR)/build/hls . $(PART) $(PERIOD_NS) \
	  $(REASONLINK) $(CURDIR)/rtl

SIMDIR := $(BD_DIR)/sataccel_bd.sim/sim_1/behav/xsim

sim-setup:
	cd $(BD_DIR) && vivado -mode batch -nojournal -log sim_setup.log \
	  -source ../../vivado/sim_setup.tcl -tclargs $(CURDIR)
	cd $(SIMDIR) && bash compile.sh > compile.out 2>&1 && bash elaborate.sh > elaborate.out 2>&1 || \
	  (tail -30 $(SIMDIR)/compile.out $(SIMDIR)/elaborate.out; exit 1)

JOBS ?= 8

# Out-of-context synthesis -> structural netlist for ESP + reports (long: ~1-2 h)
synth:
	cd $(BD_DIR) && vivado -mode batch -nojournal -log synth.log \
	  -source ../../vivado/synth_ooc.tcl -tclargs $(CURDIR) $(PERIOD_NS) $(JOBS)

build/tb_tool: tools/tb_tool.c drv/sataccel_drv.c drv/sataccel_drv.h drv/sataccel_caps.h \
               $(HANDOFF)/sataccel_image.c $(HANDOFF)/cnf.c $(HANDOFF)/trace.c
	mkdir -p build
	$(CC) $(CFLAGS) -Idrv -Idrv/hls_regs -I$(HANDOFF) -o $@ tools/tb_tool.c drv/sataccel_drv.c \
	  $(HANDOFF)/sataccel_image.c $(HANDOFF)/cnf.c $(HANDOFF)/trace.c

tb_tool: build/tb_tool

check-caps: src
	@grep -q "_FPGA_MAX_LITERALS (8192\*4)" build/src/fpga_solver.h && \
	 grep -q "_FPGA_MAX_CLAUSES 65536" build/src/fpga_solver.h && \
	 grep -q "_FPGA_MAX_LITERAL_ELEMENTS (128\*4096)" build/src/fpga_solver.h && \
	 grep -q "SA_HW_MAX_VARS              32768u" drv/sataccel_caps.h && \
	 grep -q "SA_HW_MAX_CLAUSES           65536u" drv/sataccel_caps.h && \
	 grep -q "SA_HW_MAX_LITERAL_ELEMENTS  524288u" drv/sataccel_caps.h && \
	 echo "caps OK" || (echo "caps MISMATCH between drv/sataccel_caps.h and patches/"; exit 1)

clean:
	rm -rf build

.PHONY: all src hls regs bd sim-setup synth tb_tool check-caps clean
