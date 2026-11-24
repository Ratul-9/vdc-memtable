/*
 * dat_conc.h - thread-safe DAT memtables for experiment 6 (threads at 1M - 100M keys).
 *
 * Layouts (dat_flat.h, dat_rows.h):
 *   DC_FLAT   flat array, slot = key, sized for keys 0 .. n-1
 *   DC_ROWS   two-level: row table + rows created on demand
 *
 * Synchronisation (every thread uses the memtable directly):
 *   DC_MUTEX     one global mutex
 *   DC_SPIN      65,536 striped spinlocks (64 consecutive keys share a stripe), 4 MiB, one per cache line
 *   DC_RWLOCK    the same stripes as reader-writer spinlocks
 *   DC_NODELOCK  lock byte inside the node (DatNode.lock)
 *   DC_SEQLOCK   optimistic readers on DatNode.version (even = stable), never blocked
 *                and never writing shared memory; writers claim the node with one
 *                fetch_or(version, 1) (no CAS retry loop under contention)
 *
 * Row creation (DC_ROWS) is thread-safe in every mode: the first thread to find a
 * missing row CASes its table entry from 0 to "being created", takes the next
 * arena row with an atomic add, and publishes it. Other writers of that row wait;
 * readers treat a row being created as absent (its first put has not finished).
 * No arena row is ever wasted, so the arena can be sized exactly.
 *
 * Range scans (dc_scan) walk the slots in key order; on the two-level layout,
 * rows that were never created are skipped whole.
 *
 * Values are inline (<= 32 B). get copies the value out. The caller supplies
 * sequence numbers. Status codes are the MT_* codes.
 */
#ifndef DAT_CONC_H
#define DAT_CONC_H

#include <pthread.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include "dat/dat_flat.h"
#include "dat/dat_rows.h"
#include "vdc/platform.h"

typedef enum DcLayout { DC_FLAT, DC_ROWS, DC_LAYOUT_COUNT } DcLayout;
typedef enum DcMode { DC_MUTEX, DC_SPIN, DC_RWLOCK, DC_NODELOCK, DC_SEQLOCK, DC_MODE_COUNT } DcMode;

#define DC_STRIPES      65536u
#define DC_STRIPE_SHIFT 6u          /* 64 consecutive keys per stripe */

/* One scan result (dc_scan, dv_scan). */
typedef struct DatKV {
    uint32_t key;
    uint32_t len;
    uint64_t seqno;
    uint8_t value[DAT_VALUE_BYTES];
} DatKV;

typedef struct DcLock {
    alignas(64) uint32_t word;
} DcLock;

typedef struct DatConc {
    DcLayout layout;
    DcMode mode;
    DatFlat flat;
    DatRows rows;
    pthread_mutex_t mutex;
    DcLock *stripes; /* DC_SPIN, DC_RWLOCK */
} DatConc;

/*
 * Create an empty memtable with all memory committed.
 *   flat: holds keys 0 .. n_keys-1 (rounded up to a multiple of 32,768 keys)
 *   rows: row_bits = log2(keys per row); arena sized for max_rows rows
 *         (0 = exactly what dense keys 0 .. n_keys-1 need)
 * Returns MT_OK, MT_ERR_ARG or MT_ERR_ALLOC.
 */
int dc_init(DatConc *c, DcLayout layout, DcMode mode, unsigned row_bits, uint64_t n_keys, uint64_t max_rows,
            VdcPageMode page_mode);
void dc_destroy(DatConc *c);
/* Empty the memtable. Only when no other thread uses it. */
void dc_clear(DatConc *c);

/* Thread-safe. len <= 32. */
int dc_put(DatConc *c, uint32_t key, const void *value, size_t len, uint64_t seqno);
int dc_delete(DatConc *c, uint32_t key, uint64_t seqno);
/* out holds 32 bytes or is NULL; len / seqno may be NULL. */
int dc_get(DatConc *c, uint32_t key, void *out, uint32_t *len, uint64_t *seqno);

/*
 * Thread-safe range scan: live keys in [start, end), ascending, at most limit
 * (tombstones skipped). Each key is read with the memtable's own read
 * synchronisation; keys are not read at one instant (no snapshot: see dat_mv.h).
 * Returns the number written to out; *next (if not NULL) = key to resume from.
 */
size_t dc_scan(DatConc *c, uint32_t start, uint32_t end, size_t limit, DatKV *out, uint32_t *next);

/* Keys present (tombstones included). Only when no other thread writes. */
uint64_t dc_count(const DatConc *c);
/* Memory committed by the structure (mapped bytes + stripes). */
uint64_t dc_bytes(const DatConc *c);

const char *dc_mode_name(DcMode m);

#endif /* DAT_CONC_H */
