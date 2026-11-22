/* Arena: alignment, offsets, exhaustion, reset, destroy. */
#include <stdint.h>
#include <string.h>

#include "tests.h"
#include "vdc/arena.h"

int test_arena(void)
{
    Arena a;

    CHECK(arena_init(NULL, 100) != 0);
    CHECK(arena_init(&a, 0) != 0);
    CHECK(arena_init(&a, 100) == 0);
    CHECK(a.capacity >= 100);
    CHECK(a.used == 0);

    /* 8-byte aligned bump allocation. */
    CHECK(arena_alloc(&a, 5) == 0);
    CHECK(arena_alloc(&a, 16) == 8);
    CHECK(arena_alloc(&a, 1) == 24);
    CHECK(a.used == 32);

    const uint64_t off = 8;
    memcpy(arena_ptr(&a, off), "hello world 1234", 16);
    CHECK(memcmp(arena_ptr(&a, off), "hello world 1234", 16) == 0);

    /* Failure cases. */
    CHECK(arena_alloc(&a, 0) == ARENA_FAIL);
    CHECK(arena_alloc(&a, a.capacity) == ARENA_FAIL);
    CHECK(arena_alloc(&a, (size_t)-1) == ARENA_FAIL);
    CHECK(a.used == 32); /* failures do not consume space */

    /* Exact fit, then full. */
    CHECK(arena_alloc(&a, a.capacity - a.used) != ARENA_FAIL);
    CHECK(arena_remaining(&a) == 0);
    CHECK(arena_alloc(&a, 1) == ARENA_FAIL);

    /* Reset rewinds. */
    arena_reset(&a);
    CHECK(a.used == 0);
    CHECK(arena_alloc(&a, 8) == 0);

    arena_destroy(&a);
    arena_destroy(&a); /* double destroy is safe */
    CHECK(a.base == NULL);
    return 0;
}
