/*
 * vdc.h - Van der Corput (bit reversal) slot mapping for a VDC-Space.
 *
 * Key domain : integers in [0, 2^20 - 1]  (VDC_KEY_BITS = 20)
 * Slot domain: integers in [0, 2^20 - 1]  (one slot per key)
 *
 * Every mapping here is a permutation of the bits of a 20-bit value, so it is a
 * bijection on [0, 2^20): two distinct keys never map to the same slot, and no
 * probing or hashing is needed. Lookup is O(1) worst case.
 *
 * Two mappings:
 *
 *   vdc_slot_full(key)
 *       Reverse all 20 bits. Consecutive keys land far apart.
 *
 *   vdc_slot_partial(key, j)
 *       Keep the low j bits of the key unchanged and reverse only the upper
 *       (20 - j) bits:
 *           slot = (rev_{20-j}(key >> j) << j) | (key & (2^j - 1))
 *       Keys that differ only in their low j bits stay in 2^j consecutive slots.
 *       j = 0  -> same as full reversal
 *       j = 20 -> identity (slot == key)
 *
 * Both mappings are involutions (applying one twice returns the input), so the
 * same function maps slot -> key.
 *
 * Precondition: key (or slot) < VDC_NUM_SLOTS, and j <= VDC_KEY_BITS. Define
 * VDC_DEBUG to enable assertions for these.
 */
#ifndef VDC_H
#define VDC_H

#include <stdint.h>

#ifdef VDC_DEBUG
#include <assert.h>
#define VDC_ASSERT(x) assert(x)
#else
#define VDC_ASSERT(x) ((void)0)
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#define VDC_KEY_BITS  20u
#define VDC_NUM_SLOTS (1u << VDC_KEY_BITS) /* 1048576 */
#define VDC_KEY_MASK  (VDC_NUM_SLOTS - 1u)

/* Reverse all 32 bits of x. */
static inline uint32_t vdc_rev32(uint32_t x)
{
#if __has_builtin(__builtin_bitreverse32)
    return __builtin_bitreverse32(x);
#else
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
#endif
}

/* Reverse the low nbits bits of x (0 <= nbits <= 32). x must be < 2^nbits. */
static inline uint32_t vdc_reverse_bits(uint32_t x, unsigned nbits)
{
    if (nbits == 0u)
        return 0u;
    return vdc_rev32(x) >> (32u - nbits);
}

/* Full 20-bit reversal: key -> slot. */
static inline uint32_t vdc_slot_full(uint32_t key)
{
    VDC_ASSERT(key < VDC_NUM_SLOTS);
    return vdc_reverse_bits(key, VDC_KEY_BITS);
}

/*
 * Partial reversal: key -> slot. The low j bits stay in place, the upper
 * (20 - j) bits are reversed.
 */
static inline uint32_t vdc_slot_partial(uint32_t key, unsigned j)
{
    VDC_ASSERT(key < VDC_NUM_SLOTS);
    VDC_ASSERT(j <= VDC_KEY_BITS);
    const uint32_t low_mask = (1u << j) - 1u;
    const uint32_t high = key >> j;
    return (vdc_reverse_bits(high, VDC_KEY_BITS - j) << j) | (key & low_mask);
}

/* Inverse mappings (the mappings are involutions). */
static inline uint32_t vdc_key_from_slot_full(uint32_t slot)
{
    return vdc_slot_full(slot);
}

static inline uint32_t vdc_key_from_slot_partial(uint32_t slot, unsigned j)
{
    return vdc_slot_partial(slot, j);
}

#endif /* VDC_H */
