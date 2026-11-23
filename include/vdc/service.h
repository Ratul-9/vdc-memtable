/*
 * service.h - delegation designs: producer threads never touch the memtable;
 * they send requests (put / get / delete) through lock-free queues to consumer
 * threads, and each key is owned by exactly one consumer, which performs every
 * operation on it. Owners need no locks: no two threads ever touch one Node.
 *
 *   SV_MPSC         1 consumer owns all keys; one MPSC queue.
 *   SV_SHARDED      N consumers, consumer i owns key range i of N
 *                   ([i * 2^20 / N, (i+1) * 2^20 / N)); one MPSC queue per
 *                   consumer; producers route each request to its owner.
 *   SV_SHARED_MPMC  N consumers pop from one shared MPMC queue. A request for
 *                   another consumer's range is forwarded to that consumer's
 *                   MPSC inbox. Note: forwarding can reorder two in-flight
 *                   requests on the same key from one producer.
 *
 * Results come back through a VdcCompletion (queue.h). Producers either wait
 * for each request (sync helpers below) or keep several in flight (async:
 * sv_submit + poll VdcCompletion.done).
 *
 * Consumers busy-poll their queues (one dedicated core each).
 * Values are inline only (<= 32 B). Status codes are the MT_* codes.
 */
#ifndef VDC_SERVICE_H
#define VDC_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "vdc/platform.h"
#include "vdc/queue.h"

typedef enum SvMode {
    SV_MPSC,
    SV_SHARDED,
    SV_SHARED_MPMC,
    SV_MODE_COUNT
} SvMode;

typedef struct SvConfig {
    SvMode mode;
    unsigned consumers;      /* ignored for SV_MPSC (always 1) */
    unsigned j;              /* key -> slot mapping */
    VdcPageMode page_mode;
    size_t queue_capacity;   /* per queue, power of two */
    const int *consumer_cpus; /* CPU for consumer i, or NULL = not pinned */
} SvConfig;

typedef struct VdcService VdcService;

/* Create the memtable (memory committed) and start the consumer threads. NULL on failure. */
VdcService *sv_create(const SvConfig *cfg);
/* Stop the consumers and free everything. Pending requests are dropped. */
void sv_destroy(VdcService *sv);

/* Consumer that owns key. */
unsigned sv_owner(const VdcService *sv, uint32_t key);
unsigned sv_consumers(const VdcService *sv);

/*
 * Queue one request; spins while the target queue is full. Returns MT_OK, or an
 * error (MT_ERR_KEY_RANGE, MT_ERR_ARG) in which case r->done (if set) is
 * completed immediately with that status.
 */
int sv_submit(VdcService *sv, const VdcRequest *r);

/* Synchronous helpers: submit and wait for the result. out holds 32 bytes or is NULL. */
int sv_put(VdcService *sv, uint32_t key, const void *value, size_t len, uint64_t seqno);
int sv_delete(VdcService *sv, uint32_t key, uint64_t seqno);
int sv_get(VdcService *sv, uint32_t key, void *out, uint32_t *len, uint64_t *seqno);

/* Wait until every submitted request has been applied (call when producers have stopped). */
void sv_quiesce(VdcService *sv);
/* Quiesce, then empty the memtable. */
void sv_clear(VdcService *sv);
/* Occupied slots (and tombstones). O(capacity); quiesce first. */
uint32_t sv_count(VdcService *sv, uint32_t *tombstones);
/* Requests forwarded between consumers so far (SV_SHARED_MPMC). */
uint64_t sv_forwarded(VdcService *sv);

const char *sv_mode_name(SvMode mode);

#endif /* VDC_SERVICE_H */
