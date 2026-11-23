/*
 * scale.h - shared types of the scale experiments (build/dat_scale).
 *
 * One "structure" is created at a time, measured, and destroyed before the next
 * one is created. Before creating it, the memory planner (scale_plan.c)
 * computes exactly how much memory it will need and skips it if that is over
 * the budget.
 */
#ifndef DAT_SCALE_H
#define DAT_SCALE_H

#include <stddef.h>
#include <stdint.h>

#include "bench_common.h"
#include "dat/dat_keys.h"
#include "vdc/platform.h"

enum { S_FLAT, S_ROWS, S_HASH, S_SKIPLIST, S_KEYGEN, S_KIND_COUNT };
enum { PH_INSERT, PH_GET_HIT, PH_GET_MISS, PH_UPDATE, PH_COUNT };
enum { ORD_SEQ, ORD_RAND, ORD_COUNT };

typedef struct ScaleStruct {
    int kind;
    unsigned row_bits;     /* S_ROWS only */
    VdcPageMode mode;
    char name[24];         /* "flat", "rows-4K", "rows-64K", "rows-2M", "hash", "skiplist", "keygen" */
} ScaleStruct;

typedef struct PhaseIn {
    const DatKeySet *keys;
    int order;             /* ORD_SEQ or ORD_RAND */
    const DatPerm *ord;      /* random order over 0 .. n-1 */
    const DatPerm *ord_miss; /* random order over 0 .. n_miss-1 */
    uint64_t m;            /* get_hit / update operations */
    uint64_t m_miss;       /* get_miss operations */
} PhaseIn;

typedef struct PhaseOut {
    uint64_t ops;
    uint64_t sum;
    int ok;
} PhaseOut;

typedef struct ScaleOps {
    /* Create (memory committed); NULL on failure. rows_needed: S_ROWS arena size. */
    void *(*create)(const ScaleStruct *s, uint64_t n, uint64_t rows_needed);
    void (*destroy)(void *t);
    /* Untimed: commit the pages the measurement will touch (flat on base pages). */
    void (*prepare)(void *t, const DatKeySet *k);
    /* Untimed: empty the structure before a repetition. */
    void (*clear)(void *t, const DatKeySet *k);
    void (*phase)(void *t, int op, const PhaseIn *in, PhaseOut *out);
    /* Bytes holding data right now (0 = unknown; the planner's figure is used). */
    uint64_t (*used_bytes)(void *t);
} ScaleOps;

const ScaleOps *scale_ops(int kind);

/* ---- memory planner (scale_plan.c) ---- */

#define SCALE_GIB ((uint64_t)1 << 30)
#define SCALE_MIB ((uint64_t)1 << 20)
#define SCALE_OVERHEAD_BYTES (256 * SCALE_MIB) /* program, counters, planner bitmaps */
#define SCALE_HARD_LIMIT_GIB 10.0              /* never exceeded: the run stops */

/* Rows of 2^bits[i] keys that hold at least one key of the set (one pass over the keys). */
void plan_count_rows(const DatKeySet *k, const unsigned *bits, int nb, uint64_t *used);

typedef struct ScalePlan {
    uint64_t bytes;        /* memory the structure will hold (committed) */
    uint64_t total;        /* bytes + program overhead */
    uint64_t huge_pages;   /* 2 MiB pages needed (hugetlb mode) */
    uint64_t rows_needed;  /* S_ROWS: rows to allocate */
    int run;               /* 1 = fits, 0 = skipped */
    char reason[128];
} ScalePlan;

/*
 * used_rows[0] = rows of one base page (4 KiB = 64 keys on x86), used_rows[1..3]
 * = rows of 64, 1,024 and 32,768 keys. budget: bytes allowed. huge_free: free
 * 2 MiB pages (hugetlb), -1 if unknown. skiplist_max: largest skip list run.
 */
ScalePlan plan_structure(const ScaleStruct *s, const DatKeySet *k, const uint64_t used_rows[4], uint64_t budget,
                         long huge_free, uint64_t skiplist_max);

/* Memory this process holds now: VmRSS + HugetlbPages (bytes); 0 if unknown (non-Linux). */
uint64_t scale_measured_bytes(void);

#endif /* DAT_SCALE_H */
