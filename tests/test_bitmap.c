/* Bitmap: set / clear / test / count, next_set across words and summary words, random model check. */
#include <string.h>

#include "tests.h"
#include "vdc/bitmap.h"

static unsigned char ref[VDC_BITMAP_BITS];

int test_bitmap(void)
{
    Bitmap b;

    CHECK(bitmap_init(NULL) != 0);
    CHECK(bitmap_init(&b) == 0);
    CHECK(bitmap_count(&b) == 0);
    CHECK(bitmap_next_set(&b, 0) == VDC_BITMAP_NONE);

    /* Edges of words (64 bits) and summary words (4096 bits). */
    static const uint32_t edges[] = {0, 1, 63, 64, 4095, 4096, 4097, 262143, 262144, VDC_BITMAP_BITS - 1};
    const int nedges = (int)(sizeof(edges) / sizeof(edges[0]));
    for (int i = 0; i < nedges; i++) {
        bitmap_set(&b, edges[i]);
        CHECK(bitmap_test(&b, edges[i]));
    }
    CHECK(bitmap_count(&b) == (uint32_t)nedges);
    bitmap_set(&b, edges[0]); /* setting twice changes nothing */
    CHECK(bitmap_count(&b) == (uint32_t)nedges);

    int idx = 0;
    for (uint32_t i = bitmap_next_set(&b, 0); i != VDC_BITMAP_NONE; i = bitmap_next_set(&b, i + 1)) {
        CHECK(idx < nedges && i == edges[idx]);
        idx++;
    }
    CHECK(idx == nedges);
    CHECK(bitmap_next_set(&b, VDC_BITMAP_BITS) == VDC_BITMAP_NONE);
    CHECK(bitmap_next_set(&b, 5) == 63);

    bitmap_clear(&b, 64);
    CHECK(!bitmap_test(&b, 64));
    CHECK(bitmap_count(&b) == (uint32_t)nedges - 1);
    CHECK(bitmap_next_set(&b, 64) == 4095);
    bitmap_clear(&b, 64); /* clearing twice changes nothing */
    CHECK(bitmap_count(&b) == (uint32_t)nedges - 1);

    bitmap_clear_all(&b);
    CHECK(bitmap_count(&b) == 0);
    CHECK(bitmap_next_set(&b, 0) == VDC_BITMAP_NONE);

    /* Random set/clear against a byte-per-bit model. */
    memset(ref, 0, sizeof(ref));
    uint64_t x = 88172645463325252ull;
    uint32_t count = 0;
    for (int i = 0; i < 300000; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        /* Cluster indexes so whole words get emptied again. */
        const uint32_t k = (uint32_t)(x % 20000u) * 37u % VDC_BITMAP_BITS;
        if ((x >> 40) & 1u) {
            bitmap_set(&b, k);
            count += !ref[k];
            ref[k] = 1;
        } else {
            bitmap_clear(&b, k);
            count -= ref[k];
            ref[k] = 0;
        }
    }
    CHECK(bitmap_count(&b) == count);

    uint32_t expect = 0;
    while (expect < VDC_BITMAP_BITS && !ref[expect])
        expect++;
    for (uint32_t i = bitmap_next_set(&b, 0); i != VDC_BITMAP_NONE; i = bitmap_next_set(&b, i + 1)) {
        CHECK(i == expect);
        expect++;
        while (expect < VDC_BITMAP_BITS && !ref[expect])
            expect++;
    }
    CHECK(expect == VDC_BITMAP_BITS);
    for (uint32_t i = 0; i < VDC_BITMAP_BITS; i += 1)
        CHECK(bitmap_test(&b, i) == ref[i]);

    bitmap_destroy(&b);
    bitmap_destroy(&b); /* double destroy is safe */
    CHECK(b.words == NULL);
    return 0;
}
