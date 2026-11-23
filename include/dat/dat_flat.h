/*
 * dat_flat.h - the flat array: slot = key (identity mapping), one 64 B node per
 * possible key. The reference design of the scale experiments.
 *
 * Base pages: the whole key range is only RESERVED (vdc_map_lazy). A page gets
 * real memory the first time it is written, so memory grows with the 4 KiB
 * pages that hold keys (the CPU's page table plays the role of the row table).
 * Call dat_flat_touch for every key before measuring, so that no page fault
 * happens during a measurement.
 *
 * Hugetlb pages: the mapping is committed at once (hugetlb cannot be reserved
 * lazily without risk), so capacity must be just the keys used (dense keys).
 */
#ifndef DAT_FLAT_H
#define DAT_FLAT_H

#include <stdint.h>
#include <string.h>

#include "dat/dat_node.h"
#include "vdc/platform.h"

typedef struct DatFlat {
    DatNode *nodes;
    uint64_t capacity; /* keys 0 .. capacity-1 */
    size_t map_bytes;
    VdcPageMode mode;
    int lazy;          /* 1 = base pages, committed on first write */
} DatFlat;

/* capacity <= DAT_DOMAIN. Returns MT_OK, MT_ERR_ARG or MT_ERR_ALLOC. */
int dat_flat_init(DatFlat *f, uint64_t capacity, VdcPageMode mode);
void dat_flat_destroy(DatFlat *f);

/* Commit the page holding key's node (untimed setup). */
static inline void dat_flat_touch(DatFlat *f, uint32_t key)
{
    if (key < f->capacity)
        ((volatile uint8_t *)&f->nodes[key])[63] = 0;
}

/* Empty key's node (untimed reset between repetitions). */
static inline void dat_flat_reset(DatFlat *f, uint32_t key)
{
    if (key < f->capacity)
        memset(&f->nodes[key], 0, sizeof(DatNode));
}

static inline int dat_flat_put(DatFlat *f, uint32_t key, const void *value, size_t len, uint64_t seqno)
{
    if (key >= f->capacity)
        return MT_ERR_KEY_RANGE;
    if (len > DAT_VALUE_BYTES || (len > 0 && value == NULL))
        return MT_ERR_ARG;
    dat_node_write(&f->nodes[key], key, value, len, seqno, DAT_F_OCCUPIED);
    return MT_OK;
}

static inline int dat_flat_get(const DatFlat *f, uint32_t key, void *out, uint32_t *len)
{
    if (key >= f->capacity)
        return MT_ERR_KEY_RANGE;
    return dat_node_read(&f->nodes[key], out, len);
}

#endif /* DAT_FLAT_H */
