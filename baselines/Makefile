# P1 baselines (serial, double-buffered) with cycle counters.
#   make            native build -> build/native/rl_baselines
#   make run        all four configurations on the full dataset + summary
#   make riscv      static RISC-V Linux build for Ariane on ESP
#                   (default riscv64-unknown-linux-gnu-, as on torino; use
#                   RISCV_CROSS=riscv64-linux-gnu- for Ubuntu's cross compiler)
#   make sataccel   native build -> build/sataccel/rl_baselines, which also
#                   offers --solver sataccel (SAT-Accel in functional
#                   simulation, ../sataccel_sim; host only)
#   make run-sataccel  the four configurations with --solver sataccel -> results/p2
HANDOFF := ../handoff
MINISAT := ../third_party/minisat
CROSS   ?=
CC      := $(CROSS)gcc
CXX     := $(CROSS)g++
BUILD   ?= build/native
CFLAGS  ?= -O2 -std=gnu11 -Wall -Wextra -pthread
CXXFLAGS ?= -O2 -std=c++11 -w -pthread
LDFLAGS ?= -pthread
# EXTRA_INC: `make riscv` adds compat/ (zlib.h stub for MiniSat's headers)
EXTRA_INC ?=
INC     := $(EXTRA_INC) -I$(HANDOFF) -I.
# DEFS / EXTRA_LIBS: `make sataccel` adds -DRL_WITH_SATACCEL and the SAT-Accel library
DEFS    ?=
EXTRA_LIBS ?=
SATACCEL_SIM := ../sataccel_sim
# -fpermissive: MiniSat 2013 headers (see handoff/Makefile)
MS_FLAGS := -I$(MINISAT) -D__STDC_LIMIT_MACROS -D__STDC_FORMAT_MACROS -fpermissive

C_SRC  := harness.c $(HANDOFF)/sataccel_image.c $(HANDOFF)/cnf.c $(HANDOFF)/trace.c
CC_SRC := $(HANDOFF)/solver_minisat.cc $(MINISAT)/minisat/core/Solver.cc \
          $(MINISAT)/minisat/utils/System.cc $(MINISAT)/minisat/utils/Options.cc
OBJS   := $(addprefix $(BUILD)/,$(notdir $(C_SRC:.c=.o)) $(notdir $(CC_SRC:.cc=.o)))

vpath %.c  . $(HANDOFF)
vpath %.cc $(HANDOFF) $(MINISAT)/minisat/core $(MINISAT)/minisat/utils

all: $(BUILD)/rl_baselines

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.c rl_timer.h $(wildcard $(HANDOFF)/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $(DEFS) $(INC) -c -o $@ $<

$(BUILD)/%.o: %.cc | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) $(MS_FLAGS) -c -o $@ $<

$(BUILD)/rl_baselines: $(OBJS) $(EXTRA_LIBS)
	$(CXX) -o $@ $^ $(LDFLAGS)

run: $(BUILD)/rl_baselines
	bash run_p1.sh $(BUILD)/rl_baselines

RISCV_CROSS ?= riscv64-unknown-linux-gnu-

riscv:
	$(MAKE) CROSS=$(RISCV_CROSS) BUILD=build/riscv64 EXTRA_INC=-Icompat LDFLAGS="-pthread -static"

sataccel:
	$(MAKE) -C $(SATACCEL_SIM) lib
	$(MAKE) BUILD=build/sataccel DEFS=-DRL_WITH_SATACCEL EXTRA_INC=-I$(SATACCEL_SIM) \
		EXTRA_LIBS=$(SATACCEL_SIM)/build/libsataccel.a

run-sataccel: sataccel
	SOLVER=sataccel OUT=../results/p2 bash run_p1.sh build/sataccel/rl_baselines

clean:
	rm -rf build

.PHONY: all run riscv sataccel run-sataccel clean
