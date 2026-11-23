/*
 * dat_rows.h - the two-level "matrix" with rows allocated on demand.
 *
 *   row    = key >> R          (R = row_bits; a row holds 2^R keys)
 *   column = key & (2^R - 1)
 *
 * A row table holds one 4-byte entry per possible row: 0 = row not created,
 * otherwise (row number in the arena) + 1. Rows are created the first time one
 * of their keys is written, and are taken in order from an arena, so memory
 * grows with the rows that hold keys. Every operation is 2 memory accesses
 * (table entry, then node): O(1). Keys stay in sorted order (rows by row
 * number, keys within a row by column), so ordered scans are possible later.
 *
 * Row size decides the table size for the 2^27 key range:
 *   R = 6  (4 KiB rows, 64 keys):     2,097,152 entries = 8 MiB  (L3)
 *   R = 10 (64 KiB rows, 1,024 keys):   131,072 entries = 512 KiB (L2)
 *   R = 15 (2 MiB rows, 32,768 keys):     4,096 entries = 16 KiB  (L1)
 *
 * The arena is sized up front (max_rows) and fully committed, so the program's
 * memory is known before anything runs. Single-threaded.
 */
#ifndef DAT_ROWS_H
#define DAT_ROWS_H

#include <stddef.h>
#include <stdint.h>

#include "dat/dat_node.h"
#include "vdc/platform.h"

typedef struct DatRows {
    uint32_t *table;     /* row -> arena row + 1, 0 = no row */
    uint64_t table_rows;
    size_t table_bytes;
    DatNode *arena;      /* rows in creation order */
    uint64_t arena_rows; /* capacity in rows */
    size_t arena_bytes;
    uint64_t used_rows;
    unsigned domain_bits; /* keys are < 2^domain_bits */
    unsigned row_bits;
    uint32_t row_mask;
    VdcPageMode mode;
} DatRows;

/* Returns MT_OK, MT_ERR_ARG or MT_ERR_ALLOC. max_rows = most rows that will be created. */
int dat_rows_init(DatRows *r, unsigned domain_bits, unsigned row_bits, uint64_t max_rows, VdcPageMode mode);
void dat_rows_destroy(DatRows *r);
/* Remove every row (zero the used rows and the table). Untimed reset. */
void dat_rows_clear(DatRows *r);

static inline size_t dat_rows_row_bytes(const DatRows *r) { return ((size_t)1 << r->row_bits) * sizeof(DatNode); }

/* Memory holding data: the table plus the rows created so far. */
static inline size_t dat_rows_used_bytes(const DatRows *r)
{
    return r->table_bytes + (size_t)r->used_rows * dat_rows_row_bytes(r);
}

static inline DatNode *dat_rows_node(const DatRows *r, uint32_t entry, uint32_t key)
{
    return r->arena + ((uint64_t)(entry - 1) << r->row_bits) + (key & r->row_mask);
}

static inline int dat_rows_put(DatRows *r, uint32_t key, const void *value, size_t len, uint64_t seqno)
{
    if (((uint64_t)key >> r->domain_bits) != 0)
        return MT_ERR_KEY_RANGE;
    if (len > DAT_VALUE_BYTES || (len > 0 && value == NULL))
        return MT_ERR_ARG;
    const uint32_t row = key >> r->row_bits;
    uint32_t entry = r->table[row];
    if (entry == 0) { /* first key of this row: create the row */
        if (r->used_rows == r->arena_rows)
            return MT_ERR_ARENA_FULL;
        entry = (uint32_t)++r->used_rows;
        r->table[row] = entry;
    }
    dat_node_write(dat_rows_node(r, entry, key), key, value, len, seqno, DAT_F_OCCUPIED);
    return MT_OK;
}

static inline int dat_rows_get(const DatRows *r, uint32_t key, void *out, uint32_t *len)
{
    if (((uint64_t)key >> r->domain_bits) != 0)
        return MT_ERR_KEY_RANGE;
    const uint32_t entry = r->table[key >> r->row_bits];
    if (entry == 0)
        return MT_NOT_FOUND; /* no row: the data is never touched */
    return dat_node_read(dat_rows_node(r, entry, key), out, len);
}

#endif /* DAT_ROWS_H */
