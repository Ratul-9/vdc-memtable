#include "vdc/arena.h"

#include <string.h>

int arena_init_ex(Arena *arena, size_t capacity, VdcPageMode mode, int populate)
{
    if (arena == NULL)
        return -1;
    memset(arena, 0, sizeof(*arena));
    if (capacity == 0)
        return -1;

    /* Whole pages: 4 KiB / 16 KiB base pages, or 2 MiB hugetlb pages. */
    const size_t len = vdc_map_size(capacity, mode);
    if (len == 0)
        return -1;

    void *p = vdc_map(len, mode, populate);
    if (p == NULL)
        return -1;

    arena->base = (uint8_t *)p;
    arena->capacity = len;
    arena->used = 0;
    arena->page_mode = mode;
    return 0;
}

int arena_init(Arena *arena, size_t capacity)
{
    return arena_init_ex(arena, capacity, VDC_PAGES_BASE, 0);
}

void arena_destroy(Arena *arena)
{
    if (arena == NULL || arena->base == NULL)
        return;
    vdc_unmap(arena->base, arena->capacity, arena->page_mode);
    memset(arena, 0, sizeof(*arena));
}

void arena_reset(Arena *arena)
{
    if (arena != NULL)
        arena->used = 0;
}
