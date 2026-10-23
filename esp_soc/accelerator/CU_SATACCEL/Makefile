# ESP third-party accelerator: SAT-Accel (ReasonLink). ESP's top-level make
# calls `hw` (once, if out/ is missing) and `sw` with KSRC, ARCH,
# CROSS_COMPILE (Linux) and CROSS_COMPILE_ELF set.
#
#   hw   netlist/ <- sataccel_hw's out-of-context netlist (glbl removed: ESP's
#        simulation already compiles glbl.v); out -> netlist
#   sw   sw/sataccel.ko (KSRC) and sw/sataccel_run (static, Linux)
HERE    := $(realpath .)
REPO    := $(realpath $(HERE)/../../..)
NETLIST := $(REPO)/sataccel_hw/build/vivado_bd/sataccel_top_funcsim.v

all: hw sw

hw: netlist/sataccel_top_funcsim.v
	ln -sfn netlist out

netlist/sataccel_top_funcsim.v: $(NETLIST)
	@test -f $< || { echo "missing $<: run 'make hls bd synth' in sataccel_hw"; exit 1; }
	mkdir -p netlist
	awk '/^module glbl/{skip=1} !skip{print} skip&&/^endmodule/{skip=0}' $< > $@

sw: kmd umd

kmd:
	$(MAKE) -C sw/kmd KDIR=$(KSRC) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)
	cp sw/kmd/sataccel.ko sw/

umd:
	$(MAKE) -C sw/umd CROSS_COMPILE=$(CROSS_COMPILE) REPO=$(REPO)

clean:
	-$(MAKE) -C sw/kmd KDIR=$(KSRC) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	$(MAKE) -C sw/umd clean
	rm -f sw/sataccel.ko

distclean: clean
	rm -rf netlist out

.PHONY: all hw sw kmd umd clean distclean
