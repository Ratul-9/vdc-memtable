/* Full reversal: bijection on [0, 2^20), involution, known values. */
#include <stdlib.h>
#include <string.h>

#include "tests.h"
#include "vdc/vdc.h"

int test_vdc_bijection(void)
{
    unsigned char *seen = calloc(VDC_NUM_SLOTS, 1);
    CHECK(seen != NULL);

    for (uint32_t k = 0; k < VDC_NUM_SLOTS; k++) {
        const uint32_t s = vdc_slot_full(k);
        if (s >= VDC_NUM_SLOTS || seen[s]) {
            free(seen);
            CHECK(!"slot out of range or used twice");
        }
        seen[s] = 1;
        if (vdc_key_from_slot_full(s) != k) {
            free(seen);
            CHECK(!"full reversal is not an involution");
        }
    }
    free(seen);

    /* Van der Corput values on 20 bits. */
    CHECK(vdc_slot_full(0) == 0);
    CHECK(vdc_slot_full(1) == 1u << 19);
    CHECK(vdc_slot_full(2) == 1u << 18);
    CHECK(vdc_slot_full(3) == (1u << 19) + (1u << 18));
    CHECK(vdc_slot_full(VDC_NUM_SLOTS - 1) == VDC_NUM_SLOTS - 1);

    /* Helpers. */
    CHECK(vdc_rev32(1) == 0x80000000u);
    CHECK(vdc_reverse_bits(0, 0) == 0);
    CHECK(vdc_reverse_bits(1, 1) == 1);
    CHECK(vdc_reverse_bits(1, 4) == 8);
    return 0;
}
