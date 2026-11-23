#include "vdc/queue.h"

#include <stdlib.h>
#include <string.h>

int vq_init(VdcQueue *q, size_t capacity)
{
    if (q == NULL)
        return -1;
    memset(q, 0, sizeof(*q));
    if (capacity < 2 || (capacity & (capacity - 1)) != 0)
        return -1;
    q->cells = aligned_alloc(64, capacity * sizeof(VdcCell));
    if (q->cells == NULL)
        return -1;
    memset(q->cells, 0, capacity * sizeof(VdcCell));
    for (size_t i = 0; i < capacity; i++)
        q->cells[i].seq = i;
    q->mask = capacity - 1;
    return 0;
}

void vq_destroy(VdcQueue *q)
{
    if (q == NULL)
        return;
    free(q->cells);
    q->cells = NULL;
}

int vq_push(VdcQueue *q, const VdcRequest *r)
{
    uint64_t pos = __atomic_load_n(&q->enqueue_pos, __ATOMIC_RELAXED);
    for (;;) {
        VdcCell *c = &q->cells[pos & q->mask];
        const uint64_t seq = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
        const int64_t dif = (int64_t)(seq - pos);
        if (dif == 0) {
            if (__atomic_compare_exchange_n(&q->enqueue_pos, &pos, pos + 1, 1, __ATOMIC_RELAXED,
                                            __ATOMIC_RELAXED)) {
                c->req = *r;
                __atomic_store_n(&c->seq, pos + 1, __ATOMIC_RELEASE);
                return 0;
            }
            /* pos now holds the current enqueue_pos; retry */
        } else if (dif < 0) {
            return -1; /* full */
        } else {
            pos = __atomic_load_n(&q->enqueue_pos, __ATOMIC_RELAXED);
        }
    }
}

int vq_pop_mc(VdcQueue *q, VdcRequest *out)
{
    uint64_t pos = __atomic_load_n(&q->dequeue_pos, __ATOMIC_RELAXED);
    for (;;) {
        VdcCell *c = &q->cells[pos & q->mask];
        const uint64_t seq = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
        const int64_t dif = (int64_t)(seq - (pos + 1));
        if (dif == 0) {
            if (__atomic_compare_exchange_n(&q->dequeue_pos, &pos, pos + 1, 1, __ATOMIC_RELAXED,
                                            __ATOMIC_RELAXED)) {
                *out = c->req;
                __atomic_store_n(&c->seq, pos + q->mask + 1, __ATOMIC_RELEASE);
                return 0;
            }
        } else if (dif < 0) {
            return -1; /* empty */
        } else {
            pos = __atomic_load_n(&q->dequeue_pos, __ATOMIC_RELAXED);
        }
    }
}

int vq_pop_sc(VdcQueue *q, VdcRequest *out)
{
    const uint64_t pos = __atomic_load_n(&q->dequeue_pos, __ATOMIC_RELAXED);
    VdcCell *c = &q->cells[pos & q->mask];
    if (__atomic_load_n(&c->seq, __ATOMIC_ACQUIRE) != pos + 1)
        return -1; /* empty */
    *out = c->req;
    __atomic_store_n(&c->seq, pos + q->mask + 1, __ATOMIC_RELEASE);
    __atomic_store_n(&q->dequeue_pos, pos + 1, __ATOMIC_RELAXED);
    return 0;
}

size_t vq_size(const VdcQueue *q)
{
    const uint64_t e = __atomic_load_n(&q->enqueue_pos, __ATOMIC_RELAXED);
    const uint64_t d = __atomic_load_n(&q->dequeue_pos, __ATOMIC_RELAXED);
    return e > d ? (size_t)(e - d) : 0;
}
