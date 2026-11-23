/*
 * node_ops.h - internal helpers shared by the concurrent memtables
 * (concurrent.c) and the delegation service (service.c).
 *
 * A whole Node is written and read field by field with relaxed atomic
 * accesses, so optimistic (seqlock) readers never race in the C11 sense.
 * Ordering is provided by the caller (lock acquire/release or the seqlock
 * version protocol). Values are inline only (<= 32 B).
 */
#ifndef VDC_NODE_OPS_H
#define VDC_NODE_OPS_H

#include <stdint.h>
#include <string.h>

#include "vdc/memtable.h" /* MT_* status codes */
#include "vdc/slot.h"

static inline void nops_st64(void *p, uint64_t v) { __atomic_store_n((uint64_t *)p, v, __ATOMIC_RELAXED); }
static inline uint64_t nops_ld64(const void *p) { return __atomic_load_n((const uint64_t *)p, __ATOMIC_RELAXED); }
static inline void nops_st32(void *p, uint32_t v) { __atomic_store_n((uint32_t *)p, v, __ATOMIC_RELAXED); }
static inline uint32_t nops_ld32(const void *p) { return __atomic_load_n((const uint32_t *)p, __ATOMIC_RELAXED); }

/* A copy of the fields a reader needs. */
typedef struct NodeSnap {
    uint32_t flags;
    uint32_t len;
    uint64_t seqno;
    uint64_t w[4]; /* the 32 B inline value */
} NodeSnap;

/* Write a complete entry (put: flags = OCCUPIED; delete: OCCUPIED | TOMBSTONE, len 0). */
static inline void nops_write(Node *n, uint32_t key, const void *value, size_t len, uint64_t seqno,
                              uint32_t flags)
{
    uint64_t w[4] = {0, 0, 0, 0};
    if (len > 0)
        memcpy(w, value, len);
    node_set_key(n, key); /* readers never read the key bytes */
    nops_st64(&n->seqno, seqno);
    nops_st64(&n->ext_off, 0);
    nops_st32(&n->val_len, (uint32_t)len);
    for (int i = 0; i < 4; i++)
        nops_st64(n->value + 8 * i, w[i]);
    nops_st32(&n->flags, flags);
}

static inline void nops_snap(const Node *n, NodeSnap *s)
{
    s->flags = nops_ld32(&n->flags);
    s->len = nops_ld32(&n->val_len);
    s->seqno = nops_ld64(&n->seqno);
    for (int i = 0; i < 4; i++)
        s->w[i] = nops_ld64(n->value + 8 * i);
}

/* Turn a snapshot into a get result. out must hold 32 bytes (may be NULL). */
static inline int nops_result(const NodeSnap *s, void *out, uint32_t *len, uint64_t *seqno)
{
    if (!(s->flags & NODE_F_OCCUPIED))
        return MT_NOT_FOUND;
    if (seqno != NULL)
        *seqno = s->seqno;
    if (s->flags & NODE_F_TOMBSTONE)
        return MT_DELETED;
    const uint32_t l = s->len > VDC_NODE_VALUE_BYTES ? VDC_NODE_VALUE_BYTES : s->len;
    if (out != NULL && l > 0)
        memcpy(out, s->w, l);
    if (len != NULL)
        *len = l;
    return MT_OK;
}

#endif /* VDC_NODE_OPS_H */
