/*
 * concurrent.h - thread-safe VDC memtables where every thread accesses the
 * memtable directly. Five synchronisation schemes over the same VdcSpace:
 *
 *   CM_MUTEX     one pthread mutex around every operation
 *   CM_SPIN      striped spinlocks: one per 2^VDC_STRIPE_SHIFT slots (gets lock too)
 *   CM_RWLOCK    striped reader-writer spinlocks: readers of a stripe share it
 *   CM_NODELOCK  a lock byte inside each Node (Node.lock), gets lock too
 *   CM_SEQLOCK   optimistic readers: read Node.version (even = stable), copy the
 *                Node, re-check the version, retry if a writer was active.
 *                Readers never write shared memory and never block a writer.
 *                Writers claim the Node with a CAS on the version (odd = being
 *                written), so writers to the same key still wait for each other.
 *
 * Differences from memtable.h (single-threaded):
 *   - values are inline only (<= 32 B): the arena is not thread-safe;
 *   - get copies the value out (a pointer into a Node could change under the caller);
 *   - the caller supplies the sequence number (in an LSM the write path assigns
 *     it; a shared counter here would be the bottleneck being measured, P23);
 *   - no live counters (they would be shared hot spots, P23); cm_count scans.
 * Status codes are the MT_* codes from memtable.h.
 */
#ifndef VDC_CONCURRENT_H
#define VDC_CONCURRENT_H

#include <pthread.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include "vdc/config.h"
#include "vdc/memtable.h"
#include "vdc/vdc_space.h"

typedef enum CmMode {
    CM_MUTEX,
    CM_SPIN,
    CM_RWLOCK,
    CM_NODELOCK,
    CM_SEQLOCK,
    CM_MODE_COUNT
} CmMode;

/* One lock word per 64 B line, so neighbouring stripes never false-share. */
typedef struct CmLock {
    alignas(64) uint32_t word;
} CmLock;

typedef struct CMemtable {
    VdcSpace space;
    CmMode mode;
    pthread_mutex_t mutex; /* CM_MUTEX */
    CmLock *stripes;       /* CM_SPIN, CM_RWLOCK */
    uint32_t nstripes;
} CMemtable;

/* Create an empty memtable. populate = 1 commits all memory now. Returns MT_OK, MT_ERR_ARG or MT_ERR_ALLOC. */
int cm_init(CMemtable *cm, CmMode mode, unsigned j, VdcPageMode page_mode, int populate);
void cm_destroy(CMemtable *cm);
/* Empty the memtable. Must not run concurrently with other operations. */
void cm_clear(CMemtable *cm);

/* Thread-safe operations. len <= 32. */
int cm_put(CMemtable *cm, uint32_t key, const void *value, size_t len, uint64_t seqno);
int cm_delete(CMemtable *cm, uint32_t key, uint64_t seqno);
/* out must hold 32 bytes (or be NULL). len / seqno may be NULL. */
int cm_get(CMemtable *cm, uint32_t key, void *out, uint32_t *len, uint64_t *seqno);

/* Occupied slots (and tombstones). O(capacity); only when no other thread is writing. */
uint32_t cm_count(const CMemtable *cm, uint32_t *tombstones);

const char *cm_mode_name(CmMode mode);

#endif /* VDC_CONCURRENT_H */
