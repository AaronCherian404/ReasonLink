# libsataccel + sataccel_run for Linux on Ariane (static, so the image needs
# no extra shared libraries). Invoked by ../../Makefile with CROSS_COMPILE;
# REPO is the ReasonLink checkout.
CROSS_COMPILE ?= riscv64-unknown-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
AR      := $(CROSS_COMPILE)ar
REPO    ?= $(realpath ../../../../..)
HANDOFF := $(REPO)/handoff
DRV     := $(REPO)/sataccel_hw/drv
CFLAGS  ?= -O2 -std=gnu11 -Wall -Wextra
INC     := -I. -I../kmd -I$(DRV) -I$(DRV)/hls_regs -I$(HANDOFF) -I$(REPO)/baselines
LIBSRC  := sataccel_linux.c $(DRV)/sataccel_drv.c $(HANDOFF)/sataccel_image.c $(HANDOFF)/cnf.c $(HANDOFF)/trace.c
LIBOBJ  := $(patsubst %.c,build/%.o,$(notdir $(LIBSRC)))

vpath %.c . $(DRV) $(HANDOFF)

all: ../sataccel_run ../libsataccel.a

build:
	mkdir -p $@

build/%.o: %.c | build
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

../libsataccel.a: $(LIBOBJ)
	$(AR) rcs $@ $^

../sataccel_run: build/sataccel_run.o ../libsataccel.a
	$(CC) -static -o $@ $^

clean:
	rm -rf build ../sataccel_run ../libsataccel.a

.PHONY: all clean
