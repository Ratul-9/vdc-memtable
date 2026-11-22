# VDC-Memtable Makefile
#
#   make               build everything: library, test runner, vdc_bench, experiments (nothing is run)
#   make lib           build the static library        build/libvdc.a
#   make test          build and run the unit tests
#   make debug         same as `make test` but -O0 -g, VDC_DEBUG asserts, ASan + UBSan
#   make bench         build the benchmark driver       build/vdc_bench
#   make experiments   build the experiment programs    build/exp_NN_name
#   make clean         remove build/ and build-debug/
#
# Variables: CC (default cc), CFLAGS_EXTRA (extra compiler flags), OPT (default -O2)
# The benchmarks are meant for Linux (hardware counters, hugetlb); see README.md.

CC       ?= cc
AR       ?= ar
BUILD    := build

# _GNU_SOURCE: mmap flags, sysconf, clock_gettime, perf_event_open on glibc with -std=c11.
CFLAGS   := -std=c11 -D_GNU_SOURCE -Wall -Wextra -Iinclude -Ibench -Ibaselines -Itests $(CFLAGS_EXTRA)
OPT      ?= -O2

SRC      := $(wildcard src/*.c)
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

EXP_SRC  := $(wildcard experiments/*/experiment.c)
EXP_OBJ  := $(patsubst %.c,$(BUILD)/%.o,$(EXP_SRC))
EXP_BIN  := $(patsubst experiments/%/experiment.c,$(BUILD)/exp_%,$(EXP_SRC))

ALL_OBJ  := $(OBJ) $(TEST_OBJ) $(BASE_OBJ) $(BENCH_OBJ) $(EXP_OBJ)

.PHONY: all lib test debug bench experiments clean

all: lib $(TEST_BIN) bench experiments

lib: $(LIB)

$(LIB): $(OBJ)
	$(AR) rcs $@ $^

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(OPT) -MMD -MP -c $< -o $@

$(TEST_BIN): $(TEST_OBJ) $(LIB)
	$(CC) $(CFLAGS) $(OPT) $(TEST_OBJ) $(LIB) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

bench: $(BENCH_BIN)

$(BENCH_BIN): $(BENCH_OBJ) $(BASE_OBJ) $(LIB)
	$(CC) $(CFLAGS) $(OPT) $(BENCH_OBJ) $(BASE_OBJ) $(LIB) -o $@

experiments: $(EXP_BIN)

$(BUILD)/exp_%: $(BUILD)/experiments/%/experiment.o $(BENCH_COMMON_OBJ) $(LIB)
	$(CC) $(CFLAGS) $(OPT) $^ -o $@

# Debug build in a separate directory so it does not mix with the optimized objects.
debug:
	$(MAKE) BUILD=build-debug OPT="-O0 -g -DVDC_DEBUG -fsanitize=address,undefined -fno-omit-frame-pointer" test

clean:
	rm -rf build build-debug

-include $(ALL_OBJ:.o=.d)
