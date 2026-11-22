#include "vdc/bitmap.h"

#include <stdlib.h>
#include <string.h>

int bitmap_init(Bitmap *b)
{
    if (b == NULL)
        return -1;
    memset(b, 0, sizeof(*b));
    b->words = aligned_alloc(64, VDC_BITMAP_WORDS * sizeof(uint64_t));
    if (b->words == NULL)
        return -1;
    memset(b->words, 0, VDC_BITMAP_WORDS * sizeof(uint64_t));
    return 0;
}

void bitmap_destroy(Bitmap *b)
{
    if (b == NULL)
        return;
    free(b->words);
    memset(b, 0, sizeof(*b));
}

void bitmap_clear_all(Bitmap *b)
{
    if (b == NULL || b->words == NULL)
        return;
    memset(b->words, 0, VDC_BITMAP_WORDS * sizeof(uint64_t));
    memset(b->summary, 0, sizeof(b->summary));
    b->count = 0;
}

uint32_t bitmap_next_set(const Bitmap *b, uint32_t from)
{
    if (from >= VDC_BITMAP_BITS)
        return VDC_BITMAP_NONE;

    /* Rest of the word that holds `from`. */
    const uint32_t w = from >> 6;
    const uint64_t m = b->words[w] & (~(uint64_t)0 << (from & 63u));
    if (m)
        return (w << 6) + (uint32_t)__builtin_ctzll(m);

    /* Following words, located through the summary level. */
    const uint32_t nw = w + 1;
    if (nw >= VDC_BITMAP_WORDS)
        return VDC_BITMAP_NONE;

    uint32_t s = nw >> 6;
    uint64_t sm = b->summary[s] & (~(uint64_t)0 << (nw & 63u));
    for (;;) {
        if (sm) {
            const uint32_t ww = (s << 6) + (uint32_t)__builtin_ctzll(sm);
            return (ww << 6) + (uint32_t)__builtin_ctzll(b->words[ww]);
        }
        if (++s >= VDC_BITMAP_SUMMARY_WORDS)
            return VDC_BITMAP_NONE;
        sm = b->summary[s];
    }
}
