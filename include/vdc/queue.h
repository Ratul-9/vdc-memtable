/*
 * queue.h - bounded lock-free request queues for the delegation designs.
 *
 * Dmitry Vyukov's bounded MPMC queue: a ring of 64 B cells, each with a
 * sequence number that says whether the cell is free for the producer of
 * ticket `pos` (seq == pos) or full for the consumer of ticket `pos`
 * (seq == pos + 1). Producers claim tickets with a CAS on enqueue_pos.
 *   vq_pop_mc  any number of consumers (CAS on dequeue_pos)  -> MPMC
 *   vq_pop_sc  exactly one consumer (no CAS)                 -> MPSC
 * A queue must use only one of the two pop functions.
 */
#ifndef VDC_QUEUE_H
#define VDC_QUEUE_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

enum { VDC_OP_PUT = 1, VDC_OP_GET = 2, VDC_OP_DELETE = 3 };

/* Where a consumer writes the result of one request. One cache line. */
typedef struct VdcCompletion {
    alignas(64) uint32_t done; /* 0 = pending, 1 = done (atomic, release by the consumer) */
    int32_t status;            /* MT_* code */
    uint32_t len;
    uint32_t pad_;
    uint64_t seqno;
    uint8_t value[32];         /* get result */
} VdcCompletion;

/* One operation. 56 B, so request + cell sequence fill one 64 B cache line. */
typedef struct VdcRequest {
    uint8_t op;          /* VDC_OP_* */
    uint8_t len;         /* value length, <= 32 */
    uint16_t pad_;
    uint32_t key;
    uint64_t seqno;
    VdcCompletion *done; /* NULL = no result wanted */
    uint8_t value[32];
} VdcRequest;

_Static_assert(sizeof(VdcRequest) == 56, "VdcRequest must be 56 bytes");
_Static_assert(sizeof(VdcCompletion) == 64, "VdcCompletion must be one cache line");

typedef struct VdcCell {
    alignas(64) uint64_t seq;
    VdcRequest req;
} VdcCell;

_Static_assert(sizeof(VdcCell) == 64, "VdcCell must be one cache line");

typedef struct VdcQueue {
    VdcCell *cells;
    uint64_t mask;
    alignas(64) uint64_t enqueue_pos; /* producers */
    alignas(64) uint64_t dequeue_pos; /* consumer(s) */
} VdcQueue;

/* capacity: power of two >= 2. 0 on success, -1 on failure. */
int vq_init(VdcQueue *q, size_t capacity);
void vq_destroy(VdcQueue *q);

/* Multi-producer push. 0 on success, -1 if full. */
int vq_push(VdcQueue *q, const VdcRequest *r);
/* Multi-consumer pop. 0 on success, -1 if empty. */
int vq_pop_mc(VdcQueue *q, VdcRequest *out);
/* Single-consumer pop. 0 on success, -1 if empty. */
int vq_pop_sc(VdcQueue *q, VdcRequest *out);

/* Approximate number of queued requests. */
size_t vq_size(const VdcQueue *q);

#endif /* VDC_QUEUE_H */
