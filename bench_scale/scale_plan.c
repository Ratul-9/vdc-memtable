/* Memory planner: how much memory each structure will need, computed before creating it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat/dat_node.h"
#include "scale.h"

void plan_count_rows(const DatKeySet *k, const unsigned *bits, int nb, uint64_t *used)
{
    uint8_t *maps[8] = {0};
    for (int b = 0; b < nb; b++) {
        used[b] = 0;
        const uint64_t rows = k->domain >> bits[b];
        maps[b] = calloc((size_t)(rows / 8 + 1), 1); /* at most 2^21 rows = 256 KiB */
        if (maps[b] == NULL) {
            for (int c = 0; c < b; c++)
                free(maps[c]);
            for (int c = 0; c < nb; c++)
                used[c] = k->domain >> bits[c]; /* worst case if out of memory */
            return;
        }
    }
    for (uint64_t i = 0; i < k->n; i++) {
        const uint32_t key = dat_key(k, i);
        for (int b = 0; b < nb; b++) {
            const uint64_t row = key >> bits[b];
            uint8_t *byte = &maps[b][row >> 3];
            const uint8_t bit = (uint8_t)(1u << (row & 7));
            if (!(*byte & bit)) {
                *byte |= bit;
                used[b]++;
            }
        }
    }
    for (int b = 0; b < nb; b++)
        free(maps[b]);
}

static uint64_t round_up(uint64_t x, uint64_t unit) { return (x + unit - 1) / unit * unit; }

static double gib(uint64_t b) { return (double)b / (double)SCALE_GIB; }

ScalePlan plan_structure(const ScaleStruct *s, const DatKeySet *k, const uint64_t used_rows[4], uint64_t budget,
                         long huge_free, uint64_t skiplist_max)
{
    ScalePlan p;
    memset(&p, 0, sizeof(p));
    const uint64_t page = vdc_page_size();
    const int huge = (s->mode == VDC_PAGES_HUGETLB);
    const uint64_t unit = huge ? VDC_HUGE_PAGE_SIZE : page;
    const uint64_t n = k->n;

    switch (s->kind) {
    case S_FLAT:
        if (huge) {
            if (k->pattern != DAT_DENSE) {
                p.bytes = round_up(k->domain * sizeof(DatNode), unit);
                p.total = p.bytes + SCALE_OVERHEAD_BYTES;
                snprintf(p.reason, sizeof(p.reason),
                         "flat on huge pages must commit the whole key range (8 GiB); dense keys only");
                return p;
            }
            p.bytes = round_up(n * sizeof(DatNode), unit);
        } else {
            /* Only the 4 KiB pages that hold keys get memory. */
            p.bytes = used_rows[0] * page;
        }
        break;
    case S_ROWS: {
        const int idx = s->row_bits == 6 ? 1 : s->row_bits == 10 ? 2 : 3;
        p.rows_needed = used_rows[idx];
        const uint64_t table = round_up((k->domain >> s->row_bits) * sizeof(uint32_t), unit);
        const uint64_t arena = round_up(p.rows_needed * ((uint64_t)sizeof(DatNode) << s->row_bits), unit);
        p.bytes = table + arena;
        break;
    }
    case S_HASH: {
        /* Smallest power of two with load factor <= 0.75, 64 B per entry. */
        uint64_t cap = 1024;
        while (cap * 3 < n * 4)
            cap *= 2;
        p.bytes = round_up(cap * 64, unit);
        break;
    }
    case S_SKIPLIST:
        if (n > skiplist_max) {
            p.bytes = round_up(n * 96 + 65536, unit);
            p.total = p.bytes + SCALE_OVERHEAD_BYTES;
            snprintf(p.reason, sizeof(p.reason), "skip list limited to %llu keys (run time)",
                     (unsigned long long)skiplist_max);
            return p;
        }
        p.bytes = round_up(n * 96 + 65536, unit);
        break;
    case S_KEYGEN:
        p.bytes = 0;
        break;
    default:
        snprintf(p.reason, sizeof(p.reason), "unknown structure");
        return p;
    }

    p.total = p.bytes + SCALE_OVERHEAD_BYTES;
    p.huge_pages = huge ? p.bytes / VDC_HUGE_PAGE_SIZE : 0;

    if (p.total > budget) {
        snprintf(p.reason, sizeof(p.reason), "needs %.2f GiB, budget %.2f GiB", gib(p.total), gib(budget));
        return p;
    }
    if (huge && huge_free >= 0 && p.huge_pages > (uint64_t)huge_free) {
        snprintf(p.reason, sizeof(p.reason), "needs %llu huge pages, %ld free",
                 (unsigned long long)p.huge_pages, huge_free);
        return p;
    }
    p.run = 1;
    snprintf(p.reason, sizeof(p.reason), "fits: %.2f GiB of %.2f GiB", gib(p.total), gib(budget));
    return p;
}

uint64_t scale_measured_bytes(void)
{
    const long rss = bench_proc_status_kib("VmRSS");
    const long huge = bench_proc_status_kib("HugetlbPages");
    if (rss < 0)
        return 0;
    return ((uint64_t)rss + (uint64_t)(huge > 0 ? huge : 0)) * 1024u;
}
