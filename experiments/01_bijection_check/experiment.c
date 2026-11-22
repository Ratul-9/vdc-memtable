/*
 * Experiment 01: bijection check and memory footprint of the key -> slot map.
 *
 * For every j: checks exhaustively that the mapping is a bijection and an
 * involution on [0, 2^20), then counts how many 4 KiB and 2 MiB pages the first
 * N consecutive keys touch (= TLB entries needed for a dense key range).
 * Analytic: needs no special hardware. Exit code 1 if a mapping is not a bijection.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdc/slot.h"
#include "vdc/vdc.h"

static const unsigned js[] = {0, 6, 8, 11, 15, 20};
static const uint32_t windows[] = {1000, 10000, 100000, 1000000};
#define NJ (sizeof(js) / sizeof(js[0]))
#define NW (sizeof(windows) / sizeof(windows[0]))

static uint32_t pages_touched(unsigned j, uint32_t n, size_t page_bytes, unsigned char *mark, size_t npages)
{
    memset(mark, 0, npages);
    uint32_t count = 0;
    for (uint32_t k = 0; k < n; k++) {
        const size_t p = (size_t)vdc_slot_partial(k, j) * sizeof(Node) / page_bytes;
        count += !mark[p];
        mark[p] = 1;
    }
    return count;
}

int main(void)
{
    unsigned char *seen = malloc(VDC_NUM_SLOTS);
    const size_t npages4k = (size_t)VDC_NUM_SLOTS * sizeof(Node) / 4096;
    unsigned char *mark = malloc(npages4k);
    if (seen == NULL || mark == NULL)
        return 2;
    int failed = 0;

    printf("# Experiment 01: bijection check and page footprint\n\n");
    printf("Key domain 0 .. 2^20-1, 64 B Nodes, Space = 64 MiB. j = unreversed low key bits "
           "(0 = full reversal, 20 = identity).\n\n");

    printf("## Bijection\n\n| j | bijective | involution | consecutive keys per run | run size |\n|---|---|---|---:|---:|\n");
    for (size_t a = 0; a < NJ; a++) {
        const unsigned j = js[a];
        int bij = 1, inv = 1;
        memset(seen, 0, VDC_NUM_SLOTS);
        for (uint32_t k = 0; k < VDC_NUM_SLOTS; k++) {
            const uint32_t s = vdc_slot_partial(k, j);
            if (s >= VDC_NUM_SLOTS || seen[s])
                bij = 0;
            else
                seen[s] = 1;
            if (vdc_slot_partial(s, j) != k)
                inv = 0;
        }
        failed |= !bij || !inv;
        const unsigned long long run = 1ull << j;
        printf("| %u | %s | %s | %llu | %llu B |\n", j, bij ? "✅ yes" : "❌ NO", inv ? "✅ yes" : "❌ NO", run,
               run * sizeof(Node));
    }

    static const struct {
        const char *name;
        size_t bytes;
    } pages[] = {{"4 KiB", 4096}, {"2 MiB", 2u * 1024 * 1024}};
    for (size_t p = 0; p < 2; p++) {
        printf("\n## %s pages touched by keys 0..N-1\n\nFewer pages = fewer TLB entries for a dense key range. "
               "Minimum = N x 64 B / page size.\n\n| j |",
               pages[p].name);
        for (size_t w = 0; w < NW; w++)
            printf(" N = %u |", windows[w]);
        printf("\n|---|");
        for (size_t w = 0; w < NW; w++)
            printf("---:|");
        printf("\n| minimum |");
        for (size_t w = 0; w < NW; w++)
            printf(" %llu |", ((unsigned long long)windows[w] * sizeof(Node) + pages[p].bytes - 1) / pages[p].bytes);
        printf("\n");
        for (size_t a = 0; a < NJ; a++) {
            printf("| %u |", js[a]);
            for (size_t w = 0; w < NW; w++)
                printf(" %u |", pages_touched(js[a], windows[w], pages[p].bytes, mark, npages4k));
            printf("\n");
        }
    }

    free(seen);
    free(mark);
    return failed;
}
