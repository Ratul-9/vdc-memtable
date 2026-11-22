/*
 * Experiment 06: memory commit, fill and reuse cost of one memtable (P1, P2, P9, P10, P28).
 *
 * For each page mode and populate setting: time to create the memtable, memory
 * resident afterwards, time and page faults to fill it with N keys, time to
 * clear it for reuse (instead of unmap + map) and time to destroy it.
 * Rotation (seal + switch to a new Space) is not implemented yet; this measures
 * the costs that rotation will be built from.
 *
 * Options: --keys N (default 1000000), --pages base,hugetlb (default base)
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include "bench_common.h"
#include "vdc/memtable.h"

static long minor_faults(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_minflt;
}

static void print_kib(long kib)
{
    if (kib < 0)
        printf(" n/a |");
    else
        printf(" %.1f |", (double)kib / 1024.0);
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
    static const char *const page_names[] = {"base", "hugetlb"};
    int modes[2], nmodes = bench_parse_name_list(arg_str(argc, argv, "--pages", "base"), page_names, 2, modes, 2);
    if (n == 0 || n > VDC_NUM_SLOTS || nmodes <= 0) {
        fprintf(stderr, "error: need 1 <= keys <= %u and --pages base,hugetlb\n", VDC_NUM_SLOTS);
        return 2;
    }

    uint32_t *keys = malloc(n * sizeof(uint32_t));
    if (keys == NULL)
        return 2;
    BenchRng r = {777};
    for (size_t i = 0; i < n; i++)
        keys[i] = (uint32_t)i;
    bench_shuffle_u32(keys, n, &r);

    printf("# Experiment 06: create / fill / clear / destroy cost\n\n");
    printf("%zu keys inserted in random order, 32 B inline values. RSS = VmRSS, hugetlb = HugetlbPages "
           "(from /proc/self/status; n/a elsewhere).\n\n",
           n);
    printf("| pages | populate | create ms | RSS after create MiB | hugetlb after create MiB | fill ns/op | "
           "minor faults during fill | RSS after fill MiB | clear ms | destroy ms |\n");
    printf("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|\n");

    for (int mi = 0; mi < nmodes; mi++) {
        for (int populate = 0; populate <= 1; populate++) {
            Memtable mt;
            MemtableConfig cfg = memtable_default_config();
            cfg.arena_bytes = 0;
            cfg.page_mode = (VdcPageMode)modes[mi];
            cfg.populate = populate;

            uint64_t t0 = bench_now_ns();
            if (memtable_init_config(&mt, &cfg) != MT_OK) {
                fprintf(stderr, "ERROR: cannot create memtable on %s pages (%s). For hugetlb run "
                                "sudo scripts/setup_hugepages.sh\n",
                        page_names[modes[mi]], strerror(errno));
                return 2;
            }
            const double create_ms = (double)(bench_now_ns() - t0) / 1e6;
            const long rss_create = bench_proc_status_kib("VmRSS");
            const long huge_create = bench_proc_status_kib("HugetlbPages");

            const long f0 = minor_faults();
            t0 = bench_now_ns();
            for (size_t i = 0; i < n; i++)
                memtable_put(&mt, keys[i], "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV", 32);
            const double fill_ns = (double)(bench_now_ns() - t0) / (double)n;
            const long faults = minor_faults() - f0;
            const long rss_fill = bench_proc_status_kib("VmRSS");

            t0 = bench_now_ns();
            memtable_clear(&mt);
            const double clear_ms = (double)(bench_now_ns() - t0) / 1e6;

            t0 = bench_now_ns();
            memtable_destroy(&mt);
            const double destroy_ms = (double)(bench_now_ns() - t0) / 1e6;

            printf("| %s | %s | %.2f |", page_names[modes[mi]], populate ? "yes" : "no", create_ms);
            print_kib(rss_create);
            print_kib(huge_create);
            printf(" %.1f | %ld |", fill_ns, faults);
            print_kib(rss_fill);
            printf(" %.2f | %.2f |\n", clear_ms, destroy_ms);
        }
    }

    printf("\nExpected: populate moves the page-fault cost from the first fill into create. With hugetlb, "
           "a lazy (populate = no) fill faults about 32 times (one per 2 MiB page) instead of about 16,384 "
           "(one per 4 KiB page).\n");
    free(keys);
    return 0;
}
