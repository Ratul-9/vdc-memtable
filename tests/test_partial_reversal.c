/* Partial reversal: bijection and involution for every j in 0..20, edge cases. */
#include <stdlib.h>
#include <string.h>

#include "tests.h"
#include "vdc/vdc.h"

int test_partial_reversal(void)
{
    unsigned char *seen = malloc(VDC_NUM_SLOTS);
    CHECK(seen != NULL);

    for (unsigned j = 0; j <= VDC_KEY_BITS; j++) {
        memset(seen, 0, VDC_NUM_SLOTS);
        for (uint32_t k = 0; k < VDC_NUM_SLOTS; k++) {
            const uint32_t s = vdc_slot_partial(k, j);
            if (s >= VDC_NUM_SLOTS || seen[s] || vdc_key_from_slot_partial(s, j) != k) {
                free(seen);
                CHECK(!"partial reversal is not a bijection/involution");
            }
            seen[s] = 1;
        }
    }
    free(seen);

    /* j = 0 is full reversal, j = 20 is the identity. */
    for (uint32_t k = 0; k < VDC_NUM_SLOTS; k += 7) {
        CHECK(vdc_slot_partial(k, 0) == vdc_slot_full(k));
        CHECK(vdc_slot_partial(k, VDC_KEY_BITS) == k);
    }

    /* j = 6: keys that differ only in the low 6 bits stay in one run of 64 slots. */
    for (uint32_t k = 0; k < VDC_NUM_SLOTS; k += 61) {
        const uint32_t run = vdc_slot_partial(k, 6) >> 6;
        CHECK(vdc_slot_partial(k & ~63u, 6) >> 6 == run);
        CHECK((vdc_slot_partial(k, 6) & 63u) == (k & 63u));
    }
    CHECK(vdc_slot_partial(1, 6) == 1);
    CHECK(vdc_slot_partial(64, 6) == 1u << 19);
    CHECK(vdc_slot_partial(65, 6) == (1u << 19) + 1);
    return 0;
}
