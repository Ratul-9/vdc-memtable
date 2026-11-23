/*
 * Experiment 05: Node (slot) layout report.
 *
 * Prints the decided 64 B Node layout, how Nodes map onto this machine's cache
 * lines and pages, and the size of the Space. The layout itself is enforced by
 * static asserts in slot.h and checked by tests/test_node.c.
 */
#include <stddef.h>
#include <stdio.h>

#include "bench_common.h"
#include "vdc/platform.h"
#include "vdc/slot.h"
#include "vdc/vdc_space.h"

int main(void)
{
    const int line = bench_cache_line_size();
    const size_t page = vdc_page_size();

    printf("# Experiment 05: Node layout\n\n");
    printf("| Field | Offset | Size | Purpose |\n|---|---:|---:|---|\n");
    printf("| key | %zu | %zu B | 20-bit key stored in 24 bits |\n", offsetof(Node, key), sizeof(((Node *)0)->key));
    printf("| lock | %zu | %zu B | per-node lock byte (concurrent node-lock mode) |\n", offsetof(Node, lock), sizeof(((Node *)0)->lock));
    printf("| version | %zu | %zu B | per-node version for seqlock readers |\n", offsetof(Node, version), sizeof(((Node *)0)->version));
    printf("| seqno | %zu | %zu B | write sequence number |\n", offsetof(Node, seqno), sizeof(((Node *)0)->seqno));
    printf("| ext_off | %zu | %zu B | arena offset of a value > 32 B |\n", offsetof(Node, ext_off),
           sizeof(((Node *)0)->ext_off));
    printf("| val_len | %zu | %zu B | value length |\n", offsetof(Node, val_len), sizeof(((Node *)0)->val_len));
    printf("| flags | %zu | %zu B | OCCUPIED, TOMBSTONE, EXTERNAL |\n", offsetof(Node, flags),
           sizeof(((Node *)0)->flags));
    printf("| value | %zu | %zu B | inline value |\n", offsetof(Node, value), sizeof(((Node *)0)->value));

    printf("\n| Property | Value |\n|---|---|\n");
    printf("| sizeof(Node) / alignment | %zu B / %zu B |\n", sizeof(Node), (size_t)_Alignof(Node));
    printf("| Cache line on this machine | %d B |\n", line);
    if (line >= (int)sizeof(Node))
        printf("| Nodes per cache line | %d |\n", line / (int)sizeof(Node));
    else
        printf("| Cache lines per Node | %d |\n", (int)sizeof(Node) / line);
    printf("| Base page | %zu B = %zu Nodes |\n", page, page / sizeof(Node));
    printf("| 2 MiB huge page | %zu Nodes |\n", VDC_HUGE_PAGE_SIZE / sizeof(Node));
    printf("| Slots | %u |\n", VDC_NUM_SLOTS);
    printf("| Space size | %zu B = %zu MiB = %zu base pages = %zu huge pages |\n", VDC_SPACE_BYTES,
           VDC_SPACE_BYTES >> 20, VDC_SPACE_BYTES / page, VDC_SPACE_BYTES / VDC_HUGE_PAGE_SIZE);
    return 0;
}
