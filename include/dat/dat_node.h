/*
 * dat_node.h - the slot of the scale experiments (DAT = direct address table).
 *
 * Same idea as vdc/slot.h (one 64 B cache line per key, 32 B inline value) but
 * with a full 4-byte key, so keys can go up to 2^27 (134,217,728), enough for
 * 100M keys. The existing 1M-key code keeps using vdc/slot.h unchanged.
 *
 * Layout (bytes): 0 key u32 | 4 version u32 | 8 seqno u64 | 16 val_len u16 |
 * 18 lock u8 | 19 pad | 20 flags u32 | 24 older u64 | 32 value[32].
 * version / lock: used by the concurrent memtables (dat_conc.h, dat_mv.h).
 * older: link to the next older version (dat_mv.h: version index + 1, 0 = none); 0 elsewhere.
 * Status codes are the MT_* codes from vdc/memtable.h.
 */
#ifndef DAT_NODE_H
#define DAT_NODE_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vdc/memtable.h" /* MT_* status codes */

#define DAT_KEY_BITS    27u
#define DAT_DOMAIN      ((uint64_t)1 << DAT_KEY_BITS) /* keys 0 .. 134,217,727 */
#define DAT_VALUE_BYTES 32u
#define DAT_F_OCCUPIED  1u
#define DAT_F_TOMBSTONE 2u

typedef struct DatNode {
    alignas(64) uint32_t key;
    uint32_t version;
    uint64_t seqno;
    uint16_t val_len;
    uint8_t lock;
    uint8_t pad_;
    uint32_t flags;
    uint64_t older;
    uint8_t value[DAT_VALUE_BYTES];
} DatNode;

_Static_assert(sizeof(DatNode) == 64, "DatNode must be one cache line");
_Static_assert(offsetof(DatNode, version) == 4, "version must be 4-byte aligned");
_Static_assert(offsetof(DatNode, value) == 32, "value must start at offset 32");

static inline void dat_node_write(DatNode *n, uint32_t key, const void *value, size_t len, uint64_t seqno,
                                  uint32_t flags)
{
    n->key = key;
    n->seqno = seqno;
    n->val_len = (uint16_t)len;
    if (len > 0)
        memcpy(n->value, value, len);
    n->flags = flags;
}

/* out holds 32 bytes or is NULL; len may be NULL. */
static inline int dat_node_read(const DatNode *n, void *out, uint32_t *len)
{
    if (!(n->flags & DAT_F_OCCUPIED))
        return MT_NOT_FOUND;
    if (n->flags & DAT_F_TOMBSTONE)
        return MT_DELETED;
    if (out != NULL && n->val_len > 0)
        memcpy(out, n->value, n->val_len);
    if (len != NULL)
        *len = n->val_len;
    return MT_OK;
}

#endif /* DAT_NODE_H */
