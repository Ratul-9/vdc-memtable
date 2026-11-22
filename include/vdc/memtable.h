/*
 * memtable.h - the VDC memtable: one VDC-Space of 2^20 Nodes (64 MiB) plus an
 * arena for values larger than the 32 B inline value.
 *
 * Keys are integers in [0, 2^20 - 1]. All operations are O(1) worst case: one
 * key -> slot mapping and one Node access (plus a copy of the value).
 *
 * Semantics
 *   - One version per key (the newest). put overwrites in place.
 *   - delete writes a tombstone (a put with the TOMBSTONE flag and no value).
 *     Deleting a key that was never written still writes a tombstone, because
 *     it must shadow older data in lower LSM levels.
 *   - get distinguishes "never written" (MT_NOT_FOUND) from "deleted"
 *     (MT_DELETED), so a reader knows whether to look in older tables.
 *   - Values up to 32 B are stored inline in the Node. Larger values go to the
 *     arena. Overwriting an external value with one that is not longer reuses the
 *     same arena bytes; otherwise the old bytes become garbage until the memtable
 *     is flushed and discarded.
 *   - Batched get/put prefetch the Node of key i + VDC_PREFETCH_DISTANCE while
 *     working on key i, so cache misses overlap (P17).
 *   - Not thread-safe (concurrency is deferred).
 */
#ifndef VDC_MEMTABLE_H
#define VDC_MEMTABLE_H

#include <stddef.h>
#include <stdint.h>

#include "vdc/arena.h"
#include "vdc/config.h"
#include "vdc/platform.h"
#include "vdc/slot.h"
#include "vdc/vdc_space.h"

/* Placeholder arena size until the real figure is decided. */
#define MEMTABLE_DEFAULT_ARENA_BYTES ((size_t)16 * 1024 * 1024)

/* Status codes. Negative = error. */
enum {
    MT_OK = 0,
    MT_NOT_FOUND = 1,         /* get: key was never written */
    MT_DELETED = 2,           /* get: key has a tombstone */
    MT_ERR_KEY_RANGE = -1,    /* key >= 2^20 */
    MT_ERR_ARENA_FULL = -2,   /* no room for the value; nothing was changed */
    MT_ERR_ARG = -3,          /* bad argument (NULL value with len > 0, len too big) */
    MT_ERR_ALLOC = -4         /* init: could not allocate memory */
};

typedef struct Memtable {
    VdcSpace space;
    Arena arena;
    uint64_t seqno;          /* sequence number of the latest write (0 = none) */
    uint32_t num_entries;    /* occupied slots = live keys + tombstones */
    uint32_t num_tombstones; /* slots holding a delete marker */
    uint64_t value_bytes;    /* total length of the values of live keys */
} Memtable;

typedef struct MemtableConfig {
    unsigned j;            /* unreversed low key bits (0 = full reversal, 20 = identity) */
    size_t arena_bytes;    /* 0 = no arena: values > 32 B are rejected */
    VdcPageMode page_mode; /* base pages or reserved 2 MiB hugetlb pages */
    int populate;          /* 1 = commit all memory at init (no page faults later) */
} MemtableConfig;

/* j = VDC_DEFAULT_J, default arena, base pages, lazily committed. */
static inline MemtableConfig memtable_default_config(void)
{
    MemtableConfig c;
    c.j = VDC_DEFAULT_J;
    c.arena_bytes = MEMTABLE_DEFAULT_ARENA_BYTES;
    c.page_mode = VDC_PAGES_BASE;
    c.populate = 0;
    return c;
}

/* Create an empty memtable from a config. Returns MT_OK, MT_ERR_ARG or MT_ERR_ALLOC. */
int memtable_init_config(Memtable *mt, const MemtableConfig *cfg);

/*
 * Create an empty memtable on base pages. j = number of unreversed low key bits
 * (0 = full reversal, 20 = identity). arena_bytes may be 0 (then values > 32 B
 * are rejected). Returns MT_OK, MT_ERR_ARG or MT_ERR_ALLOC.
 */
int memtable_init(Memtable *mt, unsigned j, size_t arena_bytes);

/* Free all memory. Safe on a zeroed or already-destroyed memtable. */
void memtable_destroy(Memtable *mt);

/*
 * Empty the memtable but keep (and keep committed) its memory, so it can be
 * reused instead of unmapped and mapped again. Resets seqno to 0. O(capacity).
 */
void memtable_clear(Memtable *mt);

/*
 * Insert or overwrite key. len may be 0. Returns MT_OK, MT_ERR_KEY_RANGE,
 * MT_ERR_ARENA_FULL or MT_ERR_ARG. On error the memtable is unchanged.
 */
int memtable_put(Memtable *mt, uint32_t key, const void *value, size_t len);

/* Write a tombstone for key. Returns MT_OK or MT_ERR_KEY_RANGE. */
int memtable_delete(Memtable *mt, uint32_t key);

/*
 * Look up key. On MT_OK, *value points at the value bytes and *len is its
 * length (value and len may be NULL if not wanted). The pointer is valid until
 * the next put/delete of the same key or the memtable is destroyed. Returns
 * MT_OK, MT_NOT_FOUND, MT_DELETED or MT_ERR_KEY_RANGE.
 */
int memtable_get(const Memtable *mt, uint32_t key, const void **value, uint32_t *len);

/*
 * Batched get with software prefetch. For each i, status[i] gets the result of
 * memtable_get(keys[i]); values[i] / lens[i] are filled on MT_OK (NULL / 0
 * otherwise). values, lens and status may each be NULL. Returns the number of
 * keys found (MT_OK).
 */
size_t memtable_get_batch(const Memtable *mt, const uint32_t *keys, size_t n,
                          const void **values, uint32_t *lens, int *status);

/*
 * Batched put with software prefetch: memtable_put(keys[i], values[i], lens[i])
 * for i = 0 .. n-1 in order (same result as n single puts). Stops at the first
 * error and returns it; *applied (may be NULL) = number of puts done.
 */
int memtable_put_batch(Memtable *mt, const uint32_t *keys, const void *const *values,
                       const uint32_t *lens, size_t n, size_t *applied);

/* ---- statistics ---- */

/* Number of live keys (written and not deleted). */
static inline uint32_t memtable_count(const Memtable *mt)
{
    return mt->num_entries - mt->num_tombstones;
}

static inline uint32_t memtable_tombstones(const Memtable *mt) { return mt->num_tombstones; }

/* Occupied slots: live keys + tombstones. */
static inline uint32_t memtable_entries(const Memtable *mt) { return mt->num_entries; }

/* Total slots (2^20). */
static inline uint32_t memtable_capacity(const Memtable *mt)
{
    (void)mt;
    return VDC_NUM_SLOTS;
}

static inline int memtable_is_empty(const Memtable *mt) { return mt->num_entries == 0; }
static inline int memtable_is_full(const Memtable *mt) { return mt->num_entries == VDC_NUM_SLOTS; }

/* Fixed memory reserved: the Node array plus the arena capacity. */
static inline size_t memtable_memory_bytes(const Memtable *mt)
{
    return VDC_SPACE_BYTES + mt->arena.capacity;
}

/* Bytes of Node storage in use (occupied slots * 64 B). */
static inline size_t memtable_node_bytes_used(const Memtable *mt)
{
    return (size_t)mt->num_entries * sizeof(Node);
}

static inline size_t memtable_arena_used(const Memtable *mt) { return mt->arena.used; }
static inline uint64_t memtable_value_bytes(const Memtable *mt) { return mt->value_bytes; }
static inline uint64_t memtable_seqno(const Memtable *mt) { return mt->seqno; }

#endif /* VDC_MEMTABLE_H */
