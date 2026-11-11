# Urban Heat Island Simulator
# Usage: source env/modules.sh; make serial mpi hybrid; make check
#        source env/modules.sh gpu; make openacc   (on a GPU node)
# Targets for versions that do not exist yet print a message instead of failing.

CC      = gcc
MPICC   = mpicc
# Apple Silicon clang wants -mcpu=native; everything else (the cluster) -march=native.
ifeq ($(shell uname -s)-$(shell uname -m),Darwin-arm64)
ARCH    ?= -mcpu=native
else
ARCH    ?= -march=native
endif
CFLAGS  ?= -O3 $(ARCH) -std=c11 -Wall -Wextra
OMPFLAG ?= -fopenmp
LDLIBS  ?= -lm
GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
DEFS    := -DGIT_COMMIT=\"$(GIT_COMMIT)\" -Isrc -Isrc/common
COMMON  := src/common/step.c src/common/io.c src/common/diag.c

BUILD := build

.PHONY: all serial mpi hybrid openacc bench check test clean

all: serial

serial:
	@mkdir -p $(BUILD)
	@if [ -f src/serial/heat.c ]; then \
	  $(CC) $(CFLAGS) $(DEFS) src/serial/*.c $(COMMON) -o $(BUILD)/heat_serial $(LDLIBS) && echo "built $(BUILD)/heat_serial with: $(CC) $(CFLAGS)"; \
	else echo "serial: not yet (src/serial/heat.c missing)"; fi

mpi:
	@mkdir -p $(BUILD)
	@if [ -f src/mpi/heat_mpi.c ]; then \
	  $(MPICC) $(CFLAGS) $(DEFS) src/mpi/*.c $(COMMON) -o $(BUILD)/heat_mpi $(LDLIBS) && echo "built $(BUILD)/heat_mpi"; \
	else echo "mpi: not yet (src/mpi/heat_mpi.c missing)"; fi

hybrid:
	@mkdir -p $(BUILD)
	@if [ -f src/hybrid/heat_hybrid.c ]; then \
	  $(MPICC) $(CFLAGS) $(OMPFLAG) $(DEFS) src/hybrid/*.c $(COMMON) -o $(BUILD)/heat_hybrid $(LDLIBS) && echo "built $(BUILD)/heat_hybrid"; \
	else echo "hybrid: not yet (src/hybrid/heat_hybrid.c missing)"; fi

# OpenACC (P5): needs the GPU environment (source env/modules.sh gpu), built on a GPU node.
# GPU_ARCH=ccnative targets the GPU of the build node; use cc89 (Ada) or cc120 (Blackwell) to choose.
ACC      ?= nvc
GPU_ARCH ?= ccnative
ACCFLAGS ?= -O3 -acc -gpu=$(GPU_ARCH) -Minfo=accel

openacc:
	@mkdir -p $(BUILD)
	@if [ -f src/openacc/heat_openacc.c ]; then \
	  $(ACC) $(ACCFLAGS) $(DEFS) src/openacc/*.c $(COMMON) -o $(BUILD)/heat_openacc $(LDLIBS) && echo "built $(BUILD)/heat_openacc with: $(ACC) $(ACCFLAGS)"; \
	else echo "openacc: not yet (src/openacc/heat_openacc.c missing)"; fi

# P1 optimization study: kernel variants (src/opt/bench_kernels.c), self-checking.
# -fopenmp-simd enables only the "omp simd" pragmas (no threads).
bench:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fopenmp-simd $(DEFS) src/opt/bench_kernels.c $(COMMON) -o $(BUILD)/bench_kernels $(LDLIBS)
	@echo "built $(BUILD)/bench_kernels with: $(CC) $(CFLAGS) -fopenmp-simd"

# Unit tests (no optimisation flags needed, but use the same ones to test what we run).
test: serial
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(DEFS) tests/test_serial.c $(COMMON) -o $(BUILD)/test_serial $(LDLIBS)
	./$(BUILD)/test_serial
	@$(MAKE) -s bench BUILD=$(BUILD) >/dev/null
	./$(BUILD)/bench_kernels --nx 61 --ny 37 --steps 300 --reps 1 --quiet

# Compares every built version against the serial output (tools/compare.py).
check: serial
	@if [ -x tests/check.sh ]; then tests/check.sh; else echo "check: not yet (tests/check.sh missing)"; fi

clean:
	rm -rf $(BUILD)