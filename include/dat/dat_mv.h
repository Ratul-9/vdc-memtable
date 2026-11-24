/*
 * dat_mv.h - multi-version, thread-safe DAT memtable with range scans
 * (experiment 7). Per-key version chains in the style of JellyFish.
 *
 * Layout: the newest version of every key lives inline in its DatNode (flat or
 * two-level layout, exactly as in dat_conc.h), so a get of the latest value
 * costs the same single cache line as the unversioned DAT. Older versions live
 * in a version arena of 64 B records, linked newest -> oldest from
 * DatNode.older (record index + 1, 0 = none).
 *
 *   node (newest) --older--> v(seq 9) --older--> v(seq 4) --older--> 0
 *
 * Writes (one writer per key at a time, seqlock claim on the node):
 *   - empty node:            write inline.
 *   - seqno >= inline seqno: copy the inline version into a new record that
 *                            points at the old chain, then write the new
 *                            version inline (one record + one node write).
 *   - seqno <  inline seqno: an out-of-order write (a late writer): the new
 *                            record is linked into the chain at its seqno
 *                            position, so chains are ALWAYS sorted by seqno.
 *                            (JellyFish puts late versions at the head, so a
 *                            snapshot read can return the wrong version.)
 * Records are immutable once linked except their `older` link, which only an
 * out-of-order insert changes, with a release store that publishes a fully
 * written record. Records are never freed while threads run.
 *
 * Reads (never block, never write shared memory):
 *   - seqlock snapshot of the node (value + seqno + older link);
 *   - if the inline seqno <= snapshot, done (the common, one-line case);
 *   - else walk the chain to the first version with seqno <= snapshot.
 *
 * Range scans: dv_scan visits keys in ascending order (keys are the slots), so
 * a scan is a sequential walk over nodes; on the two-level layout rows that
 * were never created are skipped whole. Each key is read like dv_get at the
 * scan's snapshot. Per key the result is exact; across keys, a scan at
 * snapshot S is a consistent cut if every write with seqno <= S finished
 * before the scan started (the usual LSM "published sequence" rule).
 *
 * Old versions are reclaimed in bulk with dv_drop_history (when no thread
 * uses the memtable), e.g. after the snapshots that needed them are gone.
 * Values are inline (<= 32 B). The caller supplies sequence numbers.
 * Status codes are the MT_* codes.
 */
#ifndef DAT_MV_H
#define DAT_MV_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include "dat/dat_conc.h" /* DcLayout */
#include "dat/dat_flat.h"
#include "dat/dat_rows.h"
#include "vdc/platform.h"

#define DV_LATEST UINT64_MAX /* snapshot that sees every version */

/* An older version of a key: one 64 B cache line. */
typedef struct DatVersion {
    alignas(64) uint64_t seqno;
    uint64_t older; /* next older version (index + 1), 0 = none; atomic */
    uint32_t key;
    uint32_t flags; /* DAT_F_OCCUPIED, DAT_F_TOMBSTONE */
    uint16_t val_len;
    uint8_t pad_[6];
    uint8_t value[DAT_VALUE_BYTES];
} DatVersion;

_Static_assert(sizeof(DatVersion) == 64, "DatVersion must be one cache line");

typedef struct DatMV {
    DcLayout layout;
    DatFlat flat;
    DatRows rows;
    DatVersion *versions; /* version arena */
    uint64_t vcap;        /* records */
    size_t vbytes;
    VdcPageMode vmode;
    uint64_t vnext; /* next free record (atomic; handed out in per-thread chunks) */
    uint64_t id;    /* unique per memtable, for the per-thread chunks */
    uint64_t gen;   /* bumped by clear / drop_history: drops every thread's chunk */
} DatMV;

#define DV_CHUNK 256u /* version records a thread takes at a time */

/*
 * Create an empty memtable with all memory committed.
 *   layout / row_bits / n_keys / max_rows: as dc_init.
 *   max_versions: capacity of the version arena (older versions alive at once).
 *     A thread may hold up to DV_CHUNK - 1 records it has not used yet.
 * Returns MT_OK, MT_ERR_ARG or MT_ERR_ALLOC.
 */
int dv_init(DatMV *m, DcLayout layout, unsigned row_bits, uint64_t n_keys, uint64_t max_rows, uint64_t max_versions,
            VdcPageMode page_mode);
void dv_destroy(DatMV *m);

/* Only when no other thread uses the memtable: */
void dv_clear(DatMV *m);         /* remove every key and version */
void dv_drop_history(DatMV *m);  /* keep only the newest version of every key; empty the version arena */

/* Thread-safe. len <= 32. MT_ERR_ARENA_FULL: no room for the older version (nothing changed). */
int dv_put(DatMV *m, uint32_t key, const void *value, size_t len, uint64_t seqno);
int dv_delete(DatMV *m, uint32_t key, uint64_t seqno);

/*
 * Thread-safe. The newest version of key with seqno <= snapshot (DV_LATEST =
 * newest). MT_OK (value copied to out, which holds 32 B or is NULL),
 * MT_DELETED (a tombstone), MT_NOT_FOUND (no version visible at the snapshot)
 * or MT_ERR_KEY_RANGE. len / seqno may be NULL.
 */
int dv_get(DatMV *m, uint32_t key, uint64_t snapshot, void *out, uint32_t *len, uint64_t *seqno);

/*
 * Thread-safe. Live keys in [start, end) at the snapshot, ascending, at most
 * limit of them (tombstoned and invisible keys are skipped). Returns the number
 * written to out. If next != NULL it receives the key to resume from (end if
 * the range is done).
 */
size_t dv_scan(DatMV *m, uint32_t start, uint32_t end, size_t limit, uint64_t snapshot, DatKV *out, uint32_t *next);

/* Only when no other thread writes: */
uint64_t dv_count(const DatMV *m);         /* keys present (tombstones included) */
uint64_t dv_versions_used(const DatMV *m); /* version records handed out (incl. unused chunk ends) */
/* Chains whose seqnos are not non-increasing or whose key is wrong; *chained = older versions linked. */
uint64_t dv_check(const DatMV *m, uint64_t *chained);
/* Versions of key (inline + older), 0 if absent. */
uint64_t dv_depth(DatMV *m, uint32_t key);

/* Memory committed: nodes (+ row table) + version arena. */
uint64_t dv_bytes(const DatMV *m);

#endif /* DAT_MV_H */
