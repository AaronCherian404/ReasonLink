# Handoff code: SAT-Accel image builder (C port), solver stand-in, tools, tests.
#   make             build tools into build/
#   make golden      byte-exact golden test vs SAT-Accel's parseDIMACS
#   make solvecheck  trace -> image -> MiniSat on all puzzles vs pysat reference
# Both tests need the workload outputs in data/sudoku (see PROGRESS.md, Stage 1)
# and the Python venv (PY=... to override).
CC  ?= gcc
CXX ?= g++
CFLAGS   ?= -O2 -std=c11 -Wall -Wextra
CXXFLAGS ?= -O2 -std=c++11 -w
BUILD   := build
SAT     := ../third_party/FPGA25_SAT_Accel/src
APINT   := ../third_party/HLS_arbitrary_Precision_Types/include
MINISAT := ../third_party/minisat
DATA    := ../data/sudoku
RESULTS := ../results/p1
PY      ?= $(HOME)/venvs/reasonlink/bin/python

CORE    := $(BUILD)/sataccel_image.o $(BUILD)/cnf.o $(BUILD)/trace.o
SOLVER  := $(BUILD)/solver_minisat.o $(BUILD)/minisat_Solver.o $(BUILD)/minisat_System.o \
           $(BUILD)/minisat_Options.o
# -fpermissive: MiniSat 2013 headers declare a friend with a default argument,
# which g++ >= 7 rejects unless permissive (no source change needed).
MS_FLAGS := -I$(MINISAT) -D__STDC_LIMIT_MACROS -D__STDC_FORMAT_MACROS -fpermissive

all: $(BUILD)/image_tool $(BUILD)/golden_parse $(BUILD)/solver_check

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.c *.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/solver_minisat.o: solver_minisat.cc solver_backend.h sataccel_image.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $(MS_FLAGS) -c -o $@ $<

$(BUILD)/minisat_%.o: | $(BUILD)
	$(CXX) $(CXXFLAGS) $(MS_FLAGS) -c -o $@ $(firstword $(wildcard $(MINISAT)/minisat/core/$*.cc $(MINISAT)/minisat/utils/$*.cc))

$(BUILD)/image_tool: image_tool.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/solver_check: solver_check.c $(CORE) $(SOLVER)
	$(CXX) -O2 -o $@ -x c $< -x none $(CORE) $(SOLVER)

# parseDIMACS, cut verbatim from the pinned host.cpp (up to the next function).
$(BUILD)/parse_dimacs.inc: $(SAT)/host.cpp | $(BUILD)
	awk '/^void parseDIMACS\(/{p=1} /^bool solve\(/{p=0} p' $< > $@

$(BUILD)/golden_parse: golden_test/golden_parse.cpp $(BUILD)/parse_dimacs.inc | $(BUILD)
	$(CXX) -O1 -std=c++14 -w -I$(BUILD) -Igolden_test/stubs -I$(APINT) -I$(SAT) -o $@ $<

golden: all
	PY=$(PY) bash golden_test/run_golden.sh

solvecheck: $(BUILD)/solver_check
	$(BUILD)/solver_check $(DATA)/base.cnf $(DATA)/trace.bin $(DATA)/reference.bin $(RESULTS)/solver_check.txt

clean:
	rm -rf $(BUILD)

.PHONY: all golden solvecheck clean
