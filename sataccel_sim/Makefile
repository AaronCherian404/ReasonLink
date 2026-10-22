# SAT-Accel functional simulation: SAT-Accel's kernel sources compiled
# unmodified (FPGA_HW undefined, i.e. their software-emulation code path) and
# run as concurrent threads connected by streams, like Vitis sw_emu.
#   make            build/sataccel_check
#   make check      SAT-Accel's benchmarks + all Sudoku puzzles
#   make lib        build/libsataccel.a (kernels + backend), linked by
#                   `make sataccel` in baselines/
# HLS headers: shim/ (thread-safe hls::stream etc.) + Xilinx's open-source
# ap_int. On a machine with Vitis HLS, HLS_INC=/path/to/Vitis_HLS/<ver>/include
# compiles against the vendor headers instead of the shim.
CXX      ?= g++
CC       ?= gcc
BUILD    := build
SATACCEL := ../third_party/FPGA25_SAT_Accel/src
APINT    := ../third_party/HLS_arbitrary_Precision_Types/include
HANDOFF  := ../handoff
DATA     := ../data/sudoku
RESULTS  := ../results/p2
HLS_INC  ?=
ifeq ($(HLS_INC),)
  HLS_FLAGS := -Ishim -I$(APINT)
  HLS_DEPS  := $(wildcard shim/*.h)
else
  # Vitis HLS keeps ap_utils.h (ap_wait) under include/etc/
  HLS_FLAGS := -I$(HLS_INC) $(if $(wildcard $(HLS_INC)/etc/ap_utils.h),-I$(HLS_INC)/etc) -DHLS_STREAM_THREAD_SAFE
endif
CXXFLAGS ?= -O2 -std=c++14 -w -pthread
CFLAGS   ?= -O2 -std=c11 -Wall -Wextra

KERNELS := backtrack color copy_in decide discover learn minimize manage solver \
           clause_store_handler location_handler restart timer \
           priority_queue_functions pq_handler message
KOBJS   := $(addprefix $(BUILD)/k_,$(addsuffix .o,$(KERNELS)))
HOBJS   := $(BUILD)/sataccel_image.o $(BUILD)/cnf.o $(BUILD)/trace.o

all: $(BUILD)/sataccel_check

$(BUILD):
	mkdir -p $@

$(BUILD)/k_%.o: $(SATACCEL)/%.cpp $(HLS_DEPS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HLS_FLAGS) -I$(SATACCEL) -c -o $@ $<

$(BUILD)/%.o: $(HANDOFF)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/sataccel_backend.o: sataccel_backend.cc sataccel_backend.h $(HLS_DEPS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HLS_FLAGS) -I$(SATACCEL) -I$(HANDOFF) -c -o $@ $<

$(BUILD)/sataccel_check.o: sataccel_check.cc sataccel_backend.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(HANDOFF) -c -o $@ $<

$(BUILD)/sataccel_check: $(BUILD)/sataccel_check.o $(BUILD)/sataccel_backend.o $(KOBJS) $(HOBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

kernels: $(KOBJS)

lib: $(BUILD)/libsataccel.a

$(BUILD)/libsataccel.a: $(KOBJS) $(BUILD)/sataccel_backend.o
	rm -f $@
	ar rcs $@ $^

check: $(BUILD)/sataccel_check
	mkdir -p $(RESULTS)
	bash run_check.sh

clean:
	rm -rf $(BUILD)

.PHONY: all kernels lib check clean
