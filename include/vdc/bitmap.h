/*
 * bitmap.h - occupancy bitmap for a VDC-Space: one bit per slot (2^20 bits =
 * 128 KiB) plus a summary level with one bit per 64-bit word (2 KiB), so empty
 * regions are skipped quickly when searching for the next set bit.
 *
 * Standalone for now (decision): the memtable does not update it, so put and
 * delete stay at one Node access. It is meant for flush / ordered iteration
 * later. Indexes are slot (or key) numbers in [0, 2^20). Not thread-safe.
 */
#ifndef VDC_BITMAP_H
#define VDC_BITMAP_H

#include <stdint.h>

#include "vdc/vdc.h"

#define VDC_BITMAP_BITS          VDC_NUM_SLOTS            /* 1048576 */
#define VDC_BITMAP_WORDS         (VDC_BITMAP_BITS / 64u)  /* 16384 = 128 KiB */
#define VDC_BITMAP_SUMMARY_WORDS (VDC_BITMAP_WORDS / 64u) /* 256 = 2 KiB */
#define VDC_BITMAP_NONE          VDC_BITMAP_BITS          /* "no set bit" */

typedef struct Bitmap {
    uint64_t *words;                             /* bit i = slot i */
    uint64_t summary[VDC_BITMAP_SUMMARY_WORDS];  /* bit w = words[w] != 0 */
    uint32_t count;                              /* number of set bits */
} Bitmap;

/* Allocate an all-zero bitmap. 0 on success, -1 on failure. */
int bitmap_init(Bitmap *b);

/* Free the memory. Safe on a zeroed or already-destroyed bitmap. */
void bitmap_destroy(Bitmap *b);

/* Clear every bit. */
void bitmap_clear_all(Bitmap *b);

/* Index of the first set bit >= from, or VDC_BITMAP_NONE. */
uint32_t bitmap_next_set(const Bitmap *b, uint32_t from);

static inline int bitmap_test(const Bitmap *b, uint32_t i)
{
    VDC_ASSERT(i < VDC_BITMAP_BITS);
    return (int)((b->words[i >> 6] >> (i & 63u)) & 1u);
}

static inline void bitmap_set(Bitmap *b, uint32_t i)
{
    VDC_ASSERT(i < VDC_BITMAP_BITS);
    const uint32_t w = i >> 6;
    const uint64_t bit = (uint64_t)1 << (i & 63u);
    if (!(b->words[w] & bit)) {
        b->words[w] |= bit;
        b->summary[w >> 6] |= (uint64_t)1 << (w & 63u);
        b->count++;
    }
}

static inline void bitmap_clear(Bitmap *b, uint32_t i)
{
    VDC_ASSERT(i < VDC_BITMAP_BITS);
    const uint32_t w = i >> 6;
    const uint64_t bit = (uint64_t)1 << (i & 63u);
    if (b->words[w] & bit) {
        b->words[w] &= ~bit;
        if (b->words[w] == 0)
            b->summary[w >> 6] &= ~((uint64_t)1 << (w & 63u));
        b->count--;
    }
}

static inline uint32_t bitmap_count(const Bitmap *b) { return b->count; }

#endif /* VDC_BITMAP_H */
