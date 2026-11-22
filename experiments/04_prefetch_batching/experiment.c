/*
 * Experiment 04: software prefetch distance for batched gets (P17).
 *
 * Fills one memtable with N keys, then runs gets over all of them with a
 * rolling prefetch: while getting key i, prefetch the Node of key i + d.
 * d = 0 is plain gets. Also measures the library call memtable_get_batch
 * (d = VDC_PREFETCH_DISTANCE). Reports median ns/op and speedup vs d = 0.
 *
 * Options: --keys N (default 1000000), --j J (default 6),
 *          --pages base,hugetlb (default base), --reps R (default 7)
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_common.h"
#include "vdc/memtable.h"

static const size_t dists[] = {0, 1, 2, 4, 8, 16, 32, 64};
#define ND (sizeof(dists) / sizeof(dists[0]))
#define NROWS (ND + 1) /* + library batch */
#define CHUNK 1024

static const void *bvals[CHUNK];
static uint32_t blens[CHUNK];
static int bstatus[CHUNK];

static uint64_t run_gets(const Memtable *mt, const uint32_t *keys, size_t n, size_t d)
{
    uint64_t hits = 0;
    for (size_t i = 0; i < n; i++) {
        if (d > 0 && i + d < n)
            vdc_prefetch_r(&mt->space.slots[vdc_space_slot_of(&mt->space, keys[i + d])]);
        const void *v;
        uint32_t l;
        hits += (memtable_get(mt, keys[i], &v, &l) == MT_OK);
    }
    return hits;
}

static uint64_t run_library_batch(const Memtable *mt, const uint32_t *keys, size_t n)
{
    uint64_t hits = 0;
    for (size_t i = 0; i < n; i += CHUNK) {
        const size_t c = (n - i < CHUNK) ? n - i : CHUNK;
        hits += memtable_get_batch(mt, keys + i, c, bvals, blens, bstatus);
    }
    return hits;
}

static const char *arg_str(int argc, char **argv, const char *name, const char *def)
{
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], name) == 0)
            return argv[i + 1];
    return def;
}

int main(int argc, char **argv)
{
    const size_t n = (size_t)strtoull(arg_str(argc, argv, "--keys", "1000000"), NULL, 10);
    const unsigned j = (unsigned)atoi(arg_str(argc, argv, "--j", "6"));
    const int reps = atoi(arg_str(argc, argv, "--reps", "7"));
    const char *pages_arg = arg_str(argc, argv, "--pages", "base");
    if (n == 0 || n > VDC_NUM_SLOTS || j > VDC_KEY_BITS || reps < 1 || reps > 100) {
        fprintf(stderr, "error: need 1 <= keys <= %u, j <= %u, 1 <= reps <= 100\n", VDC_NUM_SLOTS, VDC_KEY_BITS);
        return 2;
    }

    static const char *const page_names[] = {"base", "hugetlb"};
    int modes[2], nmodes = bench_parse_name_list(pages_arg, page_names, 2, modes, 2);
    if (nmodes <= 0) {
        fprintf(stderr, "error: --pages must be base, hugetlb or base,hugetlb\n");
        return 2;
    }
    static const char *const pat_names[] = {"dense_rand", "rand"};

    uint32_t *keys = malloc(VDC_NUM_SLOTS * sizeof(uint32_t));
    uint32_t *acc = malloc(n * sizeof(uint32_t));
    if (keys == NULL || acc == NULL)
        return 2;

    double res[2][2][NROWS]; /* mode, pattern, row */
    double samples[100];

    for (int mi = 0; mi < nmodes; mi++) {
        Memtable mt;
        MemtableConfig cfg = memtable_default_config();
        cfg.j = j;
        cfg.arena_bytes = 0;
        cfg.page_mode = (VdcPageMode)modes[mi];
        cfg.populate = 1;
        if (memtable_init_config(&mt, &cfg) != MT_OK) {
            fprintf(stderr, "ERROR: cannot create memtable on %s pages (%s). For hugetlb run "
                            "sudo scripts/setup_hugepages.sh\n",
                    page_names[modes[mi]], strerror(errno));
            return 2;
        }
        for (int pi = 0; pi < 2; pi++) {
            BenchRng r = {12345u + (uint64_t)pi};
            const size_t dom = (pi == 0) ? n : VDC_NUM_SLOTS; /* dense: 0..n-1; rand: whole domain */
            for (size_t i = 0; i < dom; i++)
                keys[i] = (uint32_t)i;
            bench_shuffle_u32(keys, dom, &r);
            memcpy(acc, keys, n * sizeof(uint32_t));
            bench_shuffle_u32(acc, n, &r);

            memtable_clear(&mt);
            for (size_t i = 0; i < n; i++)
                memtable_put(&mt, keys[i], "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV", 32);

            for (size_t row = 0; row < NROWS; row++) {
                for (int rep = 0; rep < reps; rep++) {
                    const uint64_t t0 = bench_now_ns();
                    const uint64_t hits = (row < ND) ? run_gets(&mt, acc, n, dists[row]) : run_library_batch(&mt, acc, n);
                    const uint64_t t1 = bench_now_ns();
                    if (hits != n) {
                        fprintf(stderr, "ERROR: expected %zu hits, got %llu\n", n, (unsigned long long)hits);
                        return 1;
                    }
                    samples[rep] = (double)(t1 - t0) / (double)n;
                }
                res[mi][pi][row] = bench_median(samples, (size_t)reps);
            }
            fprintf(stderr, "[%s] %s done\n", page_names[modes[mi]], pat_names[pi]);
        }
        memtable_destroy(&mt);
    }

    printf("# Experiment 04: prefetch distance for batched gets\n\n");
    printf("%zu keys, j = %u, median of %d runs. Cell = ns/op (speedup vs no prefetch).\n\n| prefetch distance |", n, j,
           reps);
    for (int mi = 0; mi < nmodes; mi++)
        for (int pi = 0; pi < 2; pi++)
            printf(" %s / %s |", page_names[modes[mi]], pat_names[pi]);
    printf("\n|---|");
    for (int c = 0; c < nmodes * 2; c++)
        printf("---:|");
    printf("\n");
    for (size_t row = 0; row < NROWS; row++) {
        if (row < ND)
            printf("| %zu%s |", dists[row], dists[row] == 0 ? " (no prefetch)" : "");
        else
            printf("| memtable_get_batch (%u) |", VDC_PREFETCH_DISTANCE);
        for (int mi = 0; mi < nmodes; mi++)
            for (int pi = 0; pi < 2; pi++)
                printf(" %.1f (%.2fx) |", res[mi][pi][row], res[mi][pi][0] / res[mi][pi][row]);
        printf("\n");
    }

    free(keys);
    free(acc);
    return 0;
}
