/*
 * vdc_space.h - a VDC-Space: a flat array of VDC_NUM_SLOTS (2^20) Nodes,
 * indexed by slot 0 .. 2^20 - 1.
 *
 * Size: 2^20 * 64 B = 64 MiB. The array is page-aligned (so also 64 B
 * aligned) and zero-filled. With hugetlb pages it is exactly 32 x 2 MiB.
 *
 * Keys (0 .. 2^20 - 1) are turned into slots with the mapping in vdc.h.
 * The space stores one reversal setting, j = number of low key bits left
 * unreversed (0 = full reversal, 20 = identity).
 */
#ifndef VDC_SPACE_H
#define VDC_SPACE_H

#include <stddef.h>
#include <stdint.h>

#include "vdc/platform.h"
#include "vdc/slot.h"
#include "vdc/vdc.h"

#define VDC_SPACE_BYTES ((size_t)VDC_NUM_SLOTS * sizeof(Node))

typedef struct VdcSpace {
    Node *slots;           /* VDC_NUM_SLOTS Nodes, indexed by slot */
    unsigned j;            /* unreversed low key bits used by the key -> slot map */
    VdcPageMode page_mode; /* how the Node array is mapped */
} VdcSpace;

/* Allocate a zeroed space on base pages, lazily committed. 0 on success, -1 on failure. */
int vdc_space_init(VdcSpace *space, unsigned j);

/* Allocate with an explicit page mode; populate = 1 commits all pages now. */
int vdc_space_init_ex(VdcSpace *space, unsigned j, VdcPageMode mode, int populate);

/* Zero every Node (empty space). Memory stays mapped and committed. O(capacity). */
void vdc_space_clear(VdcSpace *space);

/* Release the memory. Safe to call on a zeroed or already-destroyed space. */
void vdc_space_destroy(VdcSpace *space);

/* Node at a given slot index (slot < VDC_NUM_SLOTS). */
static inline Node *vdc_space_node_at_slot(VdcSpace *space, uint32_t slot)
{
    VDC_ASSERT(slot < VDC_NUM_SLOTS);
    return &space->slots[slot];
}

/* Slot index for a key under this space's reversal setting. */
static inline uint32_t vdc_space_slot_of(const VdcSpace *space, uint32_t key)
{
    return vdc_slot_partial(key, space->j);
}

/* Node for a key (key < VDC_NUM_SLOTS). O(1): one mapping + one address. */
static inline Node *vdc_space_node(VdcSpace *space, uint32_t key)
{
    return vdc_space_node_at_slot(space, vdc_space_slot_of(space, key));
}

#endif /* VDC_SPACE_H */
