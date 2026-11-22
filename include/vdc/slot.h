/*
 * slot.h - the Node stored in each slot of a VDC-Space.
 *
 * One Node = exactly 64 B = one cache line on x86 / Linux ARM servers.
 * (Apple M-series has 128 B lines, so two Nodes share a line there.)
 *
 * Layout (offsets in bytes):
 *
 *    0  key[3]     u8[3] 20-bit key padded to 24 bits, little-endian
 *    3  reserved   u8[5] empty for now (must be zero)
 *    8  seqno      u64   write sequence number (newest version only)
 *   16  ext_off    u64   offset in the Space's value arena when the value does
 *                        not fit inline (valid only if NODE_F_EXTERNAL is set)
 *   24  val_len    u32   value length in bytes
 *   28  flags      u32   NODE_F_* bits
 *   32  value[32]  u8    inline value (used when val_len <= 32)
 *
 * The 32 B header + 32 B inline value make the Node exactly 64 B.
 * A zero-filled Node is an empty slot (NODE_F_OCCUPIED not set).
 *
 * PROPOSED layout: the field set beyond key/value is still open for review.
 */
#ifndef VDC_SLOT_H
#define VDC_SLOT_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#define VDC_NODE_SIZE        64u
#define VDC_NODE_VALUE_BYTES 32u
#define VDC_NODE_KEY_BYTES 3u      /* 20-bit key stored in 24 bits */
#define VDC_NODE_RESERVED_BYTES 5u /* unused, kept zero */

#define NODE_F_OCCUPIED (1u << 0) /* slot holds a live entry */
#define NODE_F_TOMBSTONE (1u << 1) /* entry is a delete marker */
#define NODE_F_EXTERNAL (1u << 2) /* value is in the arena at ext_off */

typedef struct Node {
    alignas(VDC_NODE_SIZE) uint8_t key[VDC_NODE_KEY_BYTES]; /* alignas: whole Node on a 64 B boundary */
    uint8_t reserved[VDC_NODE_RESERVED_BYTES];
    uint64_t seqno;
    uint64_t ext_off;
    uint32_t val_len;
    uint32_t flags;
    uint8_t value[VDC_NODE_VALUE_BYTES];
} Node;

_Static_assert(sizeof(Node) == VDC_NODE_SIZE, "Node must be exactly 64 bytes");
_Static_assert(alignof(Node) == VDC_NODE_SIZE, "Node must be 64 B aligned");
_Static_assert(offsetof(Node, reserved) == 3, "reserved must start at offset 3");
_Static_assert(offsetof(Node, seqno) == 8, "seqno must start at offset 8");
_Static_assert(offsetof(Node, ext_off) == 16, "ext_off must start at offset 16");
_Static_assert(offsetof(Node, val_len) == 24, "val_len must start at offset 24");
_Static_assert(offsetof(Node, flags) == 28, "flags must start at offset 28");
_Static_assert(offsetof(Node, value) == 32, "value must start at offset 32");

/* Store / load the 24-bit key (little-endian). */
static inline void node_set_key(Node *n, uint32_t key)
{
    n->key[0] = (uint8_t)key;
    n->key[1] = (uint8_t)(key >> 8);
    n->key[2] = (uint8_t)(key >> 16);
}

static inline uint32_t node_get_key(const Node *n)
{
    return (uint32_t)n->key[0] | ((uint32_t)n->key[1] << 8) |
           ((uint32_t)n->key[2] << 16);
}

#endif /* VDC_SLOT_H */
