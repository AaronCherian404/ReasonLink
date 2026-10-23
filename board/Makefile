# Board software (Linux on Ariane in the reasonlink-vcu118 ESP SoC).
#   make           build/rl_board (RISC-V Linux)
#   make host      build/host/rl_board (x86 test build: fake /dev/sataccel running
#                  SAT-Accel's functional simulation; use --nn trace)
#   make stage DEST=<dir>   rl_board + run script + data + NVDLA loadable into
#                  <dir> (the CU_SATACCEL accelerator stages it as /root/CU_SATACCEL/rl)
# Needs ESP's NVDLA user-mode runtime built (make NV_NVDLA in the SoC folder),
# data/board (workload/export_board.py) and build/nvdla/rl_lenet_ip2.nvdla
# (workload/nvdla_compile.sh).
CROSS_COMPILE ?= riscv64-unknown-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
CXX     := $(CROSS_COMPILE)g++
ESP_ROOT ?= $(HOME)/esp
REPO    := $(realpath ..)
NVDLA   := $(ESP_ROOT)/accelerators/third-party/NV_NVDLA/sw/umd
NVLIBDIR := $(NVDLA)/out/core/src/runtime/libnvdla_runtime
TILE    := $(REPO)/esp_soc/accelerator/CU_SATACCEL/sw
HANDOFF := $(REPO)/handoff
DRV     := $(REPO)/sataccel_hw/drv
INC     := -I. -I$(TILE)/kmd -I$(TILE)/umd -I$(DRV) -I$(DRV)/hls_regs -I$(HANDOFF) -I$(REPO)/baselines
CFLAGS  ?= -O2 -std=gnu11 -Wall -Wextra
CXXFLAGS ?= -O2 -std=c++11 -Wall
B       := build
C_SRC   := rl_board.c $(TILE)/umd/sataccel_linux.c $(DRV)/sataccel_drv.c \
           $(HANDOFF)/sataccel_image.c $(HANDOFF)/cnf.c $(HANDOFF)/trace.c
OBJS    := $(addprefix $(B)/,$(notdir $(C_SRC:.c=.o))) $(B)/rl_nvdla.o

vpath %.c . $(TILE)/umd $(DRV) $(HANDOFF)

all: $(B)/rl_board

$(B):
	mkdir -p $@

$(B)/%.o: %.c | $(B)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

$(B)/rl_nvdla.o: rl_nvdla.cpp rl_nvdla.h | $(B)
	$(CXX) $(CXXFLAGS) -I$(NVDLA)/core/include -I$(NVDLA)/external/include -c -o $@ $<

# libnvdla_runtime.so lives in /root/NV_NVDLA on the board (ESP's NV_NVDLA.umd)
$(B)/rl_board: $(OBJS)
	$(CXX) -o $@ $^ -L$(NVLIBDIR) -lnvdla_runtime -Wl,-rpath,/root/NV_NVDLA -lpthread

# Host test build: the same rl_board.c over host/fake_sataccel_linux.c, whose
# fake device runs sataccel_sim (built with the hardware's patched capacities)
SIM      := $(REPO)/sataccel_sim
SIMLIB   := $(SIM)/build_vcu118/libsataccel.a
HOST_SRC := rl_board.c host/fake_sataccel_linux.c host/rl_nvdla_stub.c $(DRV)/sataccel_drv.c \
            $(HANDOFF)/sataccel_image.c $(HANDOFF)/cnf.c $(HANDOFF)/trace.c
HOST_OBJ := $(addprefix $(B)/host/,$(notdir $(HOST_SRC:.c=.o)))

vpath %.c host

$(SIMLIB):
	$(MAKE) -C $(REPO)/sataccel_hw src
	$(MAKE) -C $(SIM) lib SATACCEL=../sataccel_hw/build/src BUILD=build_vcu118

$(B)/host/%.o: %.c
	mkdir -p $(B)/host
	gcc -O2 -std=gnu11 -Wall -Wextra $(INC) -I$(SIM) -c -o $@ $<

host: $(B)/host/rl_board
$(B)/host/rl_board: $(HOST_OBJ) $(SIMLIB)
	g++ -o $@ $^ -pthread

DEST ?= $(B)/stage
stage: $(B)/rl_board
	mkdir -p $(DEST)/data $(DEST)/results
	cp $(B)/rl_board run_baselines.sh $(DEST)/
	cp $(REPO)/data/board/cells.bin $(REPO)/data/board/base.cnf $(REPO)/data/board/reference.bin $(DEST)/data/
	cp $(REPO)/build/nvdla/rl_lenet_ip2.nvdla $(DEST)/

clean:
	rm -rf $(B)

.PHONY: all host stage clean
