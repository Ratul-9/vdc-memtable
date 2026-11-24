/*
 * dat_internal.h - helpers shared by the thread-safe DAT memtables
 * (dat_conc.c and dat_mv.c): spin / reader-writer / byte locks, the seqlock
 * writer claim, and thread-safe node lookup for both layouts (including
 * thread-safe row creation for the two-level layout).
 */
#ifndef DAT_INTERNAL_H
#define DAT_INTERNAL_H

#include <stdint.h>

#include "dat/dat_flat.h"
#include "dat/dat_rows.h"
#include "vdc/platform.h"

#define DAT_ROW_BUSY 0xFFFFFFFFu /* table entry: row being created */

/* ---------------- locks ---------------- */

static inline void dat_spin_lock(uint32_t *w)
{
    for (;;) {
        if (__atomic_load_n(w, __ATOMIC_RELAXED) == 0 && __atomic_exchange_n(w, 1u, __ATOMIC_ACQUIRE) == 0)
            return;
        vdc_cpu_relax();
    }
}
static inline void dat_spin_unlock(uint32_t *w) { __atomic_store_n(w, 0u, __ATOMIC_RELEASE); }

#define DAT_RW_WRITER 0x80000000u
static inline void dat_rw_read_lock(uint32_t *w)
{
    for (;;) {
        uint32_t v = __atomic_load_n(w, __ATOMIC_RELAXED);
        if (!(v & DAT_RW_WRITER) &&
            __atomic_compare_exchange_n(w, &v, v + 1, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
        vdc_cpu_relax();
    }
}
static inline void dat_rw_read_unlock(uint32_t *w) { __atomic_fetch_sub(w, 1u, __ATOMIC_RELEASE); }
static inline void dat_rw_write_lock(uint32_t *w)
{
    for (;;) {
        uint32_t v = 0;
        if (__atomic_compare_exchange_n(w, &v, DAT_RW_WRITER, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
        vdc_cpu_relax();
    }
}
static inline void dat_rw_write_unlock(uint32_t *w) { __atomic_store_n(w, 0u, __ATOMIC_RELEASE); }

static inline void dat_byte_lock(uint8_t *b)
{
    for (;;) {
        if (__atomic_load_n(b, __ATOMIC_RELAXED) == 0 && __atomic_exchange_n(b, (uint8_t)1, __ATOMIC_ACQUIRE) == 0)
            return;
        vdc_cpu_relax();
    }
}
static inline void dat_byte_unlock(uint8_t *b) { __atomic_store_n(b, (uint8_t)0, __ATOMIC_RELEASE); }

/* Seqlock writer: one fetch_or claims the node when its version was even. Returns that even version. */
static inline uint32_t dat_seq_claim(DatNode *n)
{
    for (;;) {
        const uint32_t v = __atomic_fetch_or(&n->version, 1u, __ATOMIC_ACQUIRE);
        if (!(v & 1u))
            return v;
        while (__atomic_load_n(&n->version, __ATOMIC_RELAXED) & 1u)
            vdc_cpu_relax();
    }
}
static inline void dat_seq_release(DatNode *n, uint32_t v) { __atomic_store_n(&n->version, v + 2, __ATOMIC_RELEASE); }

/* ---------------- node lookup ---------------- */

static inline int dat_key_in_range(int is_rows, const DatFlat *f, const DatRows *r, uint32_t key)
{
    return is_rows ? ((uint64_t)key >> r->domain_bits) == 0 : key < f->capacity;
}

/* Node of key for reading; NULL if its row does not exist (yet). Key must be in range. */
static inline DatNode *dat_node_for_read(int is_rows, const DatFlat *f, const DatRows *r, uint32_t key)
{
    if (!is_rows)
        return &f->nodes[key];
    const uint32_t e = __atomic_load_n(&r->table[key >> r->row_bits], __ATOMIC_ACQUIRE);
    if (e == 0 || e == DAT_ROW_BUSY)
        return NULL;
    return dat_rows_node(r, e, key);
}

/*
 * Node of key for writing; creates its row if needed (thread-safe). The first
 * thread to see a missing row CASes the entry to BUSY, takes the next arena row
 * and publishes it; other writers of that row wait. NULL = arena full.
 */
static inline DatNode *dat_node_for_write(int is_rows, DatFlat *f, DatRows *r, uint32_t key)
{
    if (!is_rows)
        return &f->nodes[key];
    uint32_t *slot = &r->table[key >> r->row_bits];
    uint32_t e = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    while (e == 0 || e == DAT_ROW_BUSY) {
        if (e == 0) {
            uint32_t expected = 0;
            if (__atomic_compare_exchange_n(slot, &expected, DAT_ROW_BUSY, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                const uint64_t idx = __atomic_fetch_add(&r->used_rows, 1, __ATOMIC_RELAXED);
                if (idx >= r->arena_rows) {
                    __atomic_fetch_sub(&r->used_rows, 1, __ATOMIC_RELAXED);
                    __atomic_store_n(slot, 0u, __ATOMIC_RELEASE);
                    return NULL;
                }
                e = (uint32_t)(idx + 1);
                __atomic_store_n(slot, e, __ATOMIC_RELEASE); /* the arena row is already zero */
                break;
            }
            e = expected;
            continue;
        }
        vdc_cpu_relax(); /* another thread is creating this row */
        e = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    }
    return dat_rows_node(r, e, key);
}

#endif /* DAT_INTERNAL_H */
