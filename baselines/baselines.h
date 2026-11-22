/*
 * baselines.h - comparison structures for the benchmarks. Same key type
 * (uint32), same 32 B inline values and the same LSM semantics as the VDC
 * memtable: newest version only, delete writes a tombstone, get returns
 * MT_OK / MT_NOT_FOUND / MT_DELETED.
 *
 *   - Hash table: open addressing with linear probing, 64 B entries, fixed
 *     power-of-two capacity (expected O(1), probe length grows with load).
 *   - Skip list: LevelDB/RocksDB style, p = 1/4, nodes in a bump arena
 *     (O(log n), the default memtable in RocksDB).
 *   - Identity mapping (slot = key) is the VDC memtable with j = 20, so it has
 *     no separate code (see identity_table.c).
 *
 * Memory comes from vdc_map(), so baselines run in the same page modes.
 * Single-threaded.
 */
#ifndef VDC_BASELINES_H
#define VDC_BASELINES_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include "vdc/memtable.h" /* MT_* status codes */
#include "vdc/platform.h"

#define BL_VALUE_BYTES 32u
#define BL_F_OCCUPIED  1u
#define BL_F_TOMBSTONE 2u

/* ---------------- hash table ---------------- */

typedef struct HtEntry {
    alignas(64) uint32_t key;
    uint32_t flags; /* 0 = empty slot */
    uint32_t val_len;
    uint32_t reserved;
    uint8_t value[BL_VALUE_BYTES];
} HtEntry;

_Static_assert(sizeof(HtEntry) == 64, "HtEntry must be one cache line");

typedef struct HashTable {
    HtEntry *entries;
    uint32_t mask;        /* capacity - 1 */
    uint32_t num_entries; /* occupied entries (live + tombstones) */
    VdcPageMode page_mode;
} HashTable;

/* capacity = 2^log2_capacity entries (1 .. 28). Returns MT_OK, MT_ERR_ARG or MT_ERR_ALLOC. */
int ht_init(HashTable *ht, unsigned log2_capacity, VdcPageMode mode, int populate);
void ht_destroy(HashTable *ht);
void ht_clear(HashTable *ht);
/* len <= 32. MT_ERR_ARENA_FULL when the table has no free entry. */
int ht_put(HashTable *ht, uint32_t key, const void *value, size_t len);
int ht_get(const HashTable *ht, uint32_t key, const void **value, uint32_t *len);
int ht_delete(HashTable *ht, uint32_t key);

/* ---------------- skip list ---------------- */

#define SL_MAX_HEIGHT 16

typedef struct SlNode {
    uint32_t key;
    uint32_t flags;
    uint32_t val_len;
    uint32_t height;
    uint8_t value[BL_VALUE_BYTES];
    struct SlNode *next[]; /* height pointers */
} SlNode;

typedef struct SkipList {
    SlNode *head;
    uint8_t *arena;
    size_t arena_bytes;
    size_t arena_used;
    uint32_t height;      /* current max height in use */
    uint32_t num_entries; /* nodes (live + tombstones) */
    uint64_t rng;
    VdcPageMode page_mode;
} SkipList;

/* arena_bytes: room for all nodes (about 60 B per key on average). */
int sl_init(SkipList *sl, size_t arena_bytes, VdcPageMode mode, int populate);
void sl_destroy(SkipList *sl);
void sl_clear(SkipList *sl);
/* len <= 32. MT_ERR_ARENA_FULL when the arena is exhausted. */
int sl_put(SkipList *sl, uint32_t key, const void *value, size_t len);
int sl_get(const SkipList *sl, uint32_t key, const void **value, uint32_t *len);
int sl_delete(SkipList *sl, uint32_t key);

#endif /* VDC_BASELINES_H */
