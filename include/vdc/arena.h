/*
 * arena.h - bump-pointer arena for values that do not fit in a Node.
 *
 * One arena per VDC-Space. Allocation just advances a pointer; nothing is freed
 * individually. The whole arena is released (arena_destroy) or rewound
 * (arena_reset) at once, when the Space is flushed. So there is no
 * fragmentation, but overwriting an external value leaves the old bytes behind
 * as garbage until the reset.
 *
 * A Node refers to its value by offset (Node.ext_off, with NODE_F_EXTERNAL) and
 * its length is kept in Node.val_len, so the arena stores no per-allocation
 * header. Offsets are used instead of pointers so they stay valid if the arena
 * memory is ever remapped.
 *
 * Every allocation is aligned to ARENA_ALIGN (8) bytes.
 * Not thread-safe (concurrency is deferred).
 */
#ifndef VDC_ARENA_H
#define VDC_ARENA_H

#include <stddef.h>
#include <stdint.h>

#include "vdc/platform.h"

#define ARENA_ALIGN 8u
#define ARENA_FAIL  UINT64_MAX /* returned by arena_alloc when out of space */

typedef struct Arena {
    uint8_t *base;         /* start of the reserved memory */
    size_t capacity;       /* total bytes reserved (whole pages of page_mode) */
    size_t used;           /* bytes handed out so far; next free offset */
    VdcPageMode page_mode; /* how the memory is mapped */
} Arena;

/*
 * Reserve capacity bytes on base pages (rounded up to whole pages), zeroed and
 * lazily committed. Returns 0 on success, -1 on failure.
 */
int arena_init(Arena *arena, size_t capacity);

/* Same with an explicit page mode; populate = 1 commits all pages now. */
int arena_init_ex(Arena *arena, size_t capacity, VdcPageMode mode, int populate);

/* Release the memory. Safe on a zeroed or already-destroyed arena. */
void arena_destroy(Arena *arena);

/* Forget all allocations (used = 0). Memory stays mapped; contents are stale. */
void arena_reset(Arena *arena);

/*
 * Allocate size bytes (size > 0). Returns the offset of the allocation, or
 * ARENA_FAIL if the arena does not have enough room.
 */
static inline uint64_t arena_alloc(Arena *arena, size_t size)
{
    const size_t aligned = (size + (ARENA_ALIGN - 1u)) & ~(size_t)(ARENA_ALIGN - 1u);

    if (size == 0 || aligned < size || aligned > arena->capacity - arena->used)
        return ARENA_FAIL;

    const uint64_t off = arena->used;
    arena->used += aligned;
    return off;
}

/* Pointer to the bytes at offset off. off must come from arena_alloc. */
static inline void *arena_ptr(const Arena *arena, uint64_t off)
{
    return arena->base + off;
}

/* Bytes still available (before alignment rounding). */
static inline size_t arena_remaining(const Arena *arena)
{
    return arena->capacity - arena->used;
}

#endif /* VDC_ARENA_H */
