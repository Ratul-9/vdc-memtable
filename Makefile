# VDC-Memtable Makefile
#
#   make               build everything: library, test runner, vdc_bench, experiments (nothing is run)
#   make lib           build the static library        build/libvdc.a
#   make test          build and run the unit tests
#   make debug         same as `make test` but -O0 -g, VDC_DEBUG asserts, ASan + UBSan
#   make tsan          unit tests under ThreadSanitizer
#   make bench         build the benchmark driver       build/vdc_bench
#   make mtbench       build the multithreaded driver   build/vdc_mtbench
#   make scale         build the scale experiments      build/dat_scale (10M-100M keys)
#   make mtscale       build experiment 6               build/dat_mtscale (threads at 1M-100M keys)
#   make experiments   build the experiment programs    build/exp_NN_name
#   make clean         remove build/ and build-debug/
#
# Variables: CC (default cc), CFLAGS_EXTRA (extra compiler flags), OPT (default -O2)
# The benchmarks are meant for Linux (hardware counters, hugetlb); see README.md.

CC       ?= cc
AR       ?= ar
BUILD    := build

# _GNU_SOURCE: mmap flags, sysconf, clock_gettime, perf_event_open, CPU pinning on glibc with -std=c11.
# -fno-strict-aliasing: Nodes are read and written as 32/64-bit words by the concurrent code.
CFLAGS   := -std=c11 -D_GNU_SOURCE -pthread -fno-strict-aliasing -Wall -Wextra -Iinclude -Ibench -Ibaselines -Itests $(CFLAGS_EXTRA)
LDLIBS   := -pthread -lm
OPT      ?= -O2

SRC      := $(wildcard src/*.c) $(wildcard src/dat/*.c)
OBJ      := $(patsubst %.c,$(BUILD)/%.o,$(SRC))
LIB      := $(BUILD)/libvdc.a

TEST_SRC := $(wildcard tests/*.c)
TEST_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(TEST_SRC))
TEST_BIN := $(BUILD)/test_runner

BASE_SRC := $(wildcard baselines/*.c)
BASE_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(BASE_SRC))

BENCH_SRC := $(wildcard bench/*.c)
BENCH_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(BENCH_SRC))
BENCH_BIN := $(BUILD)/vdc_bench
BENCH_COMMON_OBJ := $(BUILD)/bench/bench_common.o

SCALE_SRC := $(wildcard bench_scale/*.c)
SCALE_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(SCALE_SRC))
SCALE_BIN := $(BUILD)/dat_scale

MTSCALE_SRC := $(wildcard bench_mtscale/*.c)
MTSCALE_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(MTSCALE_SRC))
MTSCALE_BIN := $(BUILD)/dat_mtscale

MT_SRC   := $(wildcard bench_mt/*.c)
MT_OBJ   := $(patsubst %.c,$(BUILD)/%.o,$(MT_SRC))
MT_BIN   := $(BUILD)/vdc_mtbench

EXP_SRC  := $(wildcard experiments/*/experiment.c)
EXP_OBJ  := $(patsubst %.c,$(BUILD)/%.o,$(EXP_SRC))
EXP_BIN  := $(patsubst experiments/%/experiment.c,$(BUILD)/exp_%,$(EXP_SRC))

ALL_OBJ  := $(OBJ) $(TEST_OBJ) $(BASE_OBJ) $(BENCH_OBJ) $(EXP_OBJ) $(MT_OBJ) $(SCALE_OBJ) $(MTSCALE_OBJ)

.PHONY: all lib test debug tsan bench mtbench scale mtscale experiments clean

all: lib $(TEST_BIN) bench mtbench scale mtscale experiments

lib: $(LIB)

$(LIB): $(OBJ)
	$(AR) rcs $@ $^

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(OPT) -MMD -MP -c $< -o $@

$(TEST_BIN): $(TEST_OBJ) $(LIB)
	$(CC) $(CFLAGS) $(OPT) $(TEST_OBJ) $(LIB) -o $@ $(LDLIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

bench: $(BENCH_BIN)

$(BENCH_BIN): $(BENCH_OBJ) $(BASE_OBJ) $(LIB)
	$(CC) $(CFLAGS) $(OPT) $(BENCH_OBJ) $(BASE_OBJ) $(LIB) -o $@ $(LDLIBS)

mtbench: $(MT_BIN)

$(MT_BIN): $(MT_OBJ) $(BENCH_COMMON_OBJ) $(LIB)
	$(CC) $(CFLAGS) $(OPT) $(MT_OBJ) $(BENCH_COMMON_OBJ) $(LIB) -o $@ $(LDLIBS)

scale: $(SCALE_BIN)

# Scale experiments link the hash table and skip list baselines (not bench/'s adapters).
$(SCALE_BIN): $(SCALE_OBJ) $(BENCH_COMMON_OBJ) $(BASE_OBJ) $(LIB)
	$(CC) $(CFLAGS) $(OPT) $(SCALE_OBJ) $(BENCH_COMMON_OBJ) $(BASE_OBJ) $(LIB) -o $@ $(LDLIBS)

mtscale: $(MTSCALE_BIN)

$(MTSCALE_BIN): $(MTSCALE_OBJ) $(BENCH_COMMON_OBJ) $(LIB)
	$(CC) $(CFLAGS) $(OPT) $(MTSCALE_OBJ) $(BENCH_COMMON_OBJ) $(LIB) -o $@ $(LDLIBS)

experiments: $(EXP_BIN)

$(BUILD)/exp_%: $(BUILD)/experiments/%/experiment.o $(BENCH_COMMON_OBJ) $(LIB)
	$(CC) $(CFLAGS) $(OPT) $^ -o $@ $(LDLIBS)

# Debug build in a separate directory so it does not mix with the optimized objects.
debug:
	$(MAKE) BUILD=build-debug OPT="-O0 -g -DVDC_DEBUG -fsanitize=address,undefined -fno-omit-frame-pointer" test

# Unit tests under ThreadSanitizer (data races in the concurrent code).
tsan:
	$(MAKE) BUILD=build-tsan OPT="-O1 -g -fsanitize=thread" test

clean:
	rm -rf build build-debug build-tsan

-include $(ALL_OBJ:.o=.d)
