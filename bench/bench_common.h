/*
 * bench_common.h - shared benchmark helpers: timer, RNG, hardware counters
 * (Linux perf_event_open), system / cache information, list parsing.
 *
 * Hardware counters are opened in small groups ("sets") that fit in the PMU at
 * the same time, so short measurements are exact instead of multiplexed. A run
 * measures one set at a time; callers repeat the run once per set.
 * On non-Linux systems no counters are available and only timings are reported.
 */
#ifndef VDC_BENCH_COMMON_H
#define VDC_BENCH_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* ---- timer ---- */

static inline uint64_t bench_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ---- RNG (splitmix64) ---- */

typedef struct BenchRng {
    uint64_t s;
} BenchRng;

static inline uint64_t bench_rng_next(BenchRng *r)
{
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* Uniform-enough value in [0, n). */
static inline uint64_t bench_rng_below(BenchRng *r, uint64_t n)
{
    return bench_rng_next(r) % n;
}

/* Fisher-Yates shuffle. */
void bench_shuffle_u32(uint32_t *a, size_t n, BenchRng *r);

/* ---- hardware counters ---- */

enum {
    PC_CYCLES,
    PC_INSTRUCTIONS,
    PC_L1D_LOAD_MISS,
    PC_LLC_MISS,
    PC_DTLB_LOAD_MISS,
    PC_DTLB_STORE_MISS,
    PC_PAGE_FAULTS,
    PC_COUNT
};

#define PERF_MAX_SETS 3

typedef struct PerfSet {
    int fds[PC_COUNT];
    int events[PC_COUNT];
    int n;      /* events opened in this set */
    int leader; /* fd of the group leader */
} PerfSet;

typedef struct Perf {
    PerfSet sets[PERF_MAX_SETS];
    int nsets;                /* usable sets (0 = no counters) */
    int available[PC_COUNT];  /* event could be opened */
} Perf;

typedef struct PerfReading {
    double value[PC_COUNT]; /* totals for the measured region */
    int valid[PC_COUNT];
} PerfReading;

/* Open the counter sets (enable = 0 opens none). Returns the number of usable sets. */
int perf_init(Perf *p, int enable);
void perf_close(Perf *p);
/* Reset and start counting set `set` (no-op without counters). */
void perf_start(Perf *p, int set);
/* Stop set `set` and read it; events of other sets are left invalid. */
void perf_stop(Perf *p, int set, PerfReading *out);
const char *perf_event_name(int ev);

/* ---- system information ---- */

typedef struct BenchCacheLevel {
    int level;
    char type[16]; /* "Data" or "Unified" */
    long size_kib;
    int ways;
    int sets;
    int line;
} BenchCacheLevel;

/* Data/unified cache levels of CPU 0 from sysfs (Linux). Returns the count (0 if unknown). */
int bench_cache_levels(BenchCacheLevel *out, int max);
/* Cache line size in bytes (64 if unknown). */
int bench_cache_line_size(void);
/* Value of a "Field:" line in /proc/meminfo (as printed, kB or count). -1 if unknown. */
long bench_meminfo_value(const char *field);
/* Value of a "Field:" line in /proc/self/status, e.g. "VmRSS", "HugetlbPages" (kB). -1 if unknown. */
long bench_proc_status_kib(const char *field);
/* Markdown table with CPU, kernel, caches, page sizes, huge pages, perf settings, compiler. */
void bench_print_system_info(FILE *f);

/* ---- misc ---- */

/* Median of v[0..n-1] (sorts v). 0 for n == 0. */
double bench_median(double *v, size_t n);
/* Write one byte per 64 B line of buf to push other data out of the caches. */
void bench_flush_caches(unsigned char *buf, size_t bytes);
/* Parse "1K,10K,1M,42" (K = 1e3, M = 1e6). Returns the count, -1 on error. */
int bench_parse_u64_list(const char *s, uint64_t *out, int max);
/* Parse "a,b" against names[]; out[] gets indexes. Returns the count, -1 on error. */
int bench_parse_name_list(const char *s, const char *const *names, int nnames, int *out, int max);

#endif /* VDC_BENCH_COMMON_H */
