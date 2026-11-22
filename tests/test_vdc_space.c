/* VdcSpace: allocation, alignment, zero fill, key -> Node round trip. */
#include <stdint.h>
#include <string.h>

#include "tests.h"
#include "vdc/vdc_space.h"

int test_vdc_space(void)
{
    VdcSpace s;

    /* Invalid arguments. */
    CHECK(vdc_space_init(NULL, 0) != 0);
    CHECK(vdc_space_init(&s, VDC_KEY_BITS + 1) != 0);

    CHECK(vdc_space_init(&s, 6) == 0);
    CHECK(VDC_SPACE_BYTES == 64u * 1024u * 1024u);
    CHECK(((uintptr_t)s.slots % 64) == 0);

    /* Zero-filled, then one distinct write per key. */
    for (uint32_t k = 0; k < VDC_NUM_SLOTS; k++) {
        Node *n = vdc_space_node(&s, k);
        CHECK(n->flags == 0);
        node_set_key(n, k);
        memcpy(n->value, &k, sizeof(k));
        n->flags = NODE_F_OCCUPIED;
    }
    for (uint32_t k = 0; k < VDC_NUM_SLOTS; k++) {
        const Node *n = vdc_space_node(&s, k);
        uint32_t v;
        memcpy(&v, n->value, sizeof(v));
        CHECK(v == k && node_get_key(n) == k);
        CHECK(n == &s.slots[vdc_space_slot_of(&s, k)]);
    }

    vdc_space_destroy(&s);
    vdc_space_destroy(&s); /* double destroy is safe */
    CHECK(s.slots == NULL);

    /* Works for the extreme settings too. */
    CHECK(vdc_space_init(&s, 0) == 0);
    CHECK(vdc_space_node(&s, 1) == &s.slots[1u << 19]);
    vdc_space_destroy(&s);
    CHECK(vdc_space_init(&s, VDC_KEY_BITS) == 0);
    CHECK(vdc_space_node(&s, 1) == &s.slots[1]);
    vdc_space_destroy(&s);
    return 0;
}
