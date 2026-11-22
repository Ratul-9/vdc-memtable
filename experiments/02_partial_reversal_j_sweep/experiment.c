/*
 * Experiment 02 (analytic part): cache-set aliasing of the key -> slot map.
 *
 * A cache picks the set from the low bits of the line address. Bit reversal
 * moves high key bits into low slot bits, so keys that differ only in their low
 * bits can all land in one set (P16). For each j and each window of W
 * consecutive keys this prints the largest number of keys that share one set.
 * More keys than ways in one set = conflict misses even though the cache has room.
 *
 * Cache geometry comes from sysfs (Linux) or from --l1-sets/--l1-ways/
 * --l2-sets/--l2-ways/--line. With 2 MiB pages the low 21 address bits are the
 * same in virtual and physical addresses, so the set index is exact for any
 * cache indexed below bit 21. With 4 KiB pages only L1 (indexed below bit 12)
 * is exact. LLCs with hashed slice selection are not modelled.
 *
 * The measured part of the j sweep is vdc_bench (see README.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_common.h"
#include "vdc/slot.h"
#include "vdc/vdc.h"

typedef struct CacheModel {
    const char *name;
    int sets;
    int ways;
    int line;
} CacheModel;

static const unsigned js[] = {0, 6, 8, 11, 15, 20};
static const uint32_t windows[] = {64, 512, 1024, 4096, 16384, 65536};
#define NJ (sizeof(js) / sizeof(js[0]))
#define NW (sizeof(windows) / sizeof(windows[0]))

static void analyse(const CacheModel *c, const char *exact)
{
    uint32_t *count = calloc((size_t)c->sets, sizeof(*count));
    if (count == NULL)
        return;

    printf("\n## %s: %d sets x %d ways, %d B lines (%s)\n\n", c->name, c->sets, c->ways, c->line, exact);
    printf("Cell = most keys in one set out of W consecutive keys starting at 0. "
           "✅ ≤ %d ways, ❌ conflict misses.\n\n| j |",
           c->ways);
    for (size_t w = 0; w < NW; w++)
        printf(" W = %u |", windows[w]);
    printf("\n|---|");
    for (size_t w = 0; w < NW; w++)
        printf("---:|");
    printf("\n| ideal |");
    for (size_t w = 0; w < NW; w++) {
        /* W Nodes occupy W x 64 B; spread evenly over all sets. */
        const unsigned long long lines = ((unsigned long long)windows[w] * sizeof(Node) + (unsigned)c->line - 1) /
                                         (unsigned)c->line;
        printf(" %llu |", (lines + (unsigned)c->sets - 1) / (unsigned)c->sets);
    }
    printf("\n");

    for (size_t a = 0; a < NJ; a++) {
        printf("| %u |", js[a]);
        for (size_t w = 0; w < NW; w++) {
            memset(count, 0, (size_t)c->sets * sizeof(*count));
            uint32_t worst = 0;
            uint64_t last_line = UINT64_MAX;
            for (uint32_t k = 0; k < windows[w]; k++) {
                const uint64_t line = (uint64_t)vdc_slot_partial(k, js[a]) * sizeof(Node) / (uint64_t)c->line;
                if (line == last_line)
                    continue; /* two Nodes in one line (line > 64 B) count once */
                last_line = line;
                const uint32_t set = (uint32_t)(line % (uint64_t)c->sets);
                if (++count[set] > worst)
                    worst = count[set];
            }
            printf(" %u %s |", worst, worst <= (uint32_t)c->ways ? "✅" : "❌");
        }
        printf("\n");
    }
    free(count);
}

static int arg_int(int argc, char **argv, const char *name, int def)
{
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], name) == 0)
            return atoi(argv[i + 1]);
    return def;
}

int main(int argc, char **argv)
{
    CacheModel l1 = {"L1D", 64, 12, 64}, l2 = {"L2", 2048, 16, 64};
    const char *src = "defaults (no sysfs data)";

    BenchCacheLevel lv[8];
    const int n = bench_cache_levels(lv, 8);
    for (int i = 0; i < n; i++) {
        if (lv[i].sets <= 0 || lv[i].ways <= 0 || lv[i].line <= 0)
            continue;
        if (lv[i].level == 1 && strcmp(lv[i].type, "Data") == 0) {
            l1.sets = lv[i].sets;
            l1.ways = lv[i].ways;
            l1.line = lv[i].line;
            src = "this machine (sysfs)";
        } else if (lv[i].level == 2) {
            l2.sets = lv[i].sets;
            l2.ways = lv[i].ways;
            l2.line = lv[i].line;
        }
    }
    l1.sets = arg_int(argc, argv, "--l1-sets", l1.sets);
    l1.ways = arg_int(argc, argv, "--l1-ways", l1.ways);
    l2.sets = arg_int(argc, argv, "--l2-sets", l2.sets);
    l2.ways = arg_int(argc, argv, "--l2-ways", l2.ways);
    l1.line = l2.line = arg_int(argc, argv, "--line", l1.line);
    if (l1.sets <= 0 || l1.ways <= 0 || l2.sets <= 0 || l2.ways <= 0 || l1.line <= 0) {
        fprintf(stderr, "error: cache parameters must be positive\n");
        return 2;
    }

    printf("# Experiment 02: cache-set aliasing per j (analytic)\n\n");
    printf("Cache geometry: %s. Window = W consecutive keys (a dense, sequential working set).\n", src);
    analyse(&l1, "exact with any page size");
    analyse(&l2, "exact with 2 MiB pages; partly randomised by 4 KiB pages");
    return 0;
}
