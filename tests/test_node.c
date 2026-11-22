/* Node layout and the 24-bit key accessors. */
#include <stddef.h>
#include <string.h>

#include "tests.h"
#include "vdc/slot.h"

int test_node(void)
{
    CHECK(sizeof(Node) == 64);
    CHECK(alignof(Node) == 64);
    CHECK(offsetof(Node, key) == 0);
    CHECK(offsetof(Node, reserved) == 3);
    CHECK(offsetof(Node, seqno) == 8);
    CHECK(offsetof(Node, ext_off) == 16);
    CHECK(offsetof(Node, val_len) == 24);
    CHECK(offsetof(Node, flags) == 28);
    CHECK(offsetof(Node, value) == 32);

    Node n;
    memset(&n, 0, sizeof(n));
    for (uint32_t k = 0; k < (1u << 20); k++) {
        node_set_key(&n, k);
        CHECK(node_get_key(&n) == k);
    }
    node_set_key(&n, 0xFFFFFFu); /* full 24 bits */
    CHECK(node_get_key(&n) == 0xFFFFFFu);

    /* The key must not touch the reserved bytes. */
    for (int i = 0; i < 5; i++)
        CHECK(n.reserved[i] == 0);
    return 0;
}
