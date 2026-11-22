/*
 * bench.h - interface between the benchmark driver (bench_main.c) and the
 * structures under test (bench_vdc.c, bench_hash.c, bench_skiplist.c).
 *
 * Each structure provides run_phase(): one tight loop of one operation type
 * over a key list. The driver times and counts around the call, so per-op
 * calls are direct (no function pointer per operation).
 */
#ifndef VDC_BENCH_H
#define VDC_BENCH_H

#include <stddef.h>
#include <stdint.h>

#include "bench_common.h"
#include "vdc/platform.h"

#define BENCH_VALUE_BYTES 32u   /* inline values only (decision) */
#define BENCH_BATCH_CHUNK 1024u /* keys per memtable_get_batch call */

typedef enum BenchOp {
    OP_INSERT,    /* put N new keys into an empty table */
    OP_GET_HIT,   /* get keys that are present */
    OP_GET_MISS,  /* get keys that were never written */
    OP_GET_BATCH, /* get present keys through the prefetching batch API (vdc only) */
    OP_UPDATE,    /* overwrite present keys */
    OP_DELETE,    /* tombstone present keys */
    OP_COUNT
} BenchOp;

typedef enum BenchPattern {
    PAT_SEQ,        /* key set [0, N), every pass in ascending order */
    PAT_DENSE_RAND, /* key set [0, N), random order */
    PAT_RAND,       /* N random keys out of [0, 2^20), random order */
    PAT_COUNT
} BenchPattern;

extern const char *const bench_op_names[OP_COUNT];
extern const char *const bench_pattern_names[PAT_COUNT];
extern uint8_t bench_value_a[BENCH_VALUE_BYTES]; /* written by insert */
extern uint8_t bench_value_b[BENCH_VALUE_BYTES]; /* written by update */

typedef struct BenchKeys {
    const uint32_t *insert; /* the key set, insertion order */
    const uint32_t *access; /* the same set, order for get / update / delete */
    size_t n;
    const uint32_t *miss;   /* keys not in the set */
    size_t n_miss;
} BenchKeys;

typedef struct BenchImplConfig {
    unsigned j;            /* vdc only */
    VdcPageMode page_mode;
    size_t max_keys;       /* largest key count that will be inserted */
} BenchImplConfig;

typedef struct PhaseResult {
    uint64_t ops;      /* operations executed */
    uint64_t hits;     /* gets that returned MT_OK */
    uint64_t checksum; /* keeps the loop from being optimised away */
    int ok;            /* every operation returned the expected status */
} PhaseResult;

typedef struct BenchImpl {
    const char *name;
    int uses_j;
    int has_batch;
    void *(*create)(const BenchImplConfig *cfg); /* memory fully committed; NULL on failure */
    void (*destroy)(void *t);
    void (*clear)(void *t);
    void (*run_phase)(void *t, BenchOp op, const BenchKeys *k, size_t passes, PhaseResult *out);
} BenchImpl;

extern const BenchImpl bench_impl_vdc;
extern const BenchImpl bench_impl_hash;
extern const BenchImpl bench_impl_skiplist;

#endif /* VDC_BENCH_H */
