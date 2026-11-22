/*
 * vdc_bench - single-threaded benchmark driver for the VDC memtable and the
 * baselines. Prints a markdown report on stdout (tables) and can append the raw
 * numbers to a CSV file. Run `vdc_bench --help` for the options.
 *
 * Measurement model
 *   - For every (page mode, variant, key count, pattern) cell the table is
 *     cleared and the phases run in order: insert, get_hit, get_miss,
 *     get_batch, update, delete. Each phase is timed on its own.
 *   - Read and update phases repeat over the key list until at least
 *     --min-ops operations ran (so small key counts are measured long enough).
 *     Insert and delete run once (they change the table).
 *   - The whole sequence is repeated --reps times per counter set; reported
 *     values are medians.
 *   - All memory is committed before measuring (populate), so page faults are
 *     not part of the numbers (and are checked to be 0).
 *   - O(1) is checked with instructions per operation, which do not depend on
 *     the cache hierarchy: they must stay flat from 1K to 1M keys. Time per
 *     operation grows with the key count because the working set moves from L1
 *     to DRAM; the cache and TLB miss columns explain that growth.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"
#include "vdc/vdc.h"

/* Targets used by the Checks table. */
#define CHECK_O1_MAX_RATIO  1.10 /* max / min instructions per op across key counts */
#define CHECK_LLC_MAX       1.25 /* LLC misses per op: one 64 B Node = one line */
#define CHECK_DTLB_HUGE_MAX 0.05 /* dTLB misses per op with 2 MiB pages */

#define MAX_LIST     16
#define MAX_REPS     50
#define MAX_VARIANTS 24
#define MAX_SAMPLES  (MAX_REPS * PERF_MAX_SETS)
#define NUM_IMPLS    3

/* Pseudo metric ids next to the PC_* counter ids. */
#define M_NS   (-1)
#define M_DTLB (-2)

uint8_t bench_value_a[BENCH_VALUE_BYTES];
uint8_t bench_value_b[BENCH_VALUE_BYTES];
const char *const bench_op_names[OP_COUNT] = {"insert", "get_hit", "get_miss", "get_batch", "update", "delete"};
const char *const bench_pattern_names[PAT_COUNT] = {"seq", "dense_rand", "rand"};

static const BenchImpl *const impl_table[NUM_IMPLS] = {&bench_impl_vdc, &bench_impl_hash, &bench_impl_skiplist};
static const char *const impl_names[NUM_IMPLS] = {"vdc", "hash", "skiplist"};
static const char *const page_names[2] = {"base", "hugetlb"};

typedef struct Options {
    int impls[NUM_IMPLS];
    int nimpls;
    uint64_t js[MAX_LIST];
    int nj;
    uint64_t keys[MAX_LIST];
    int nkeys;
    int patterns[PAT_COUNT];
    int npatterns;
    int report_op[OP_COUNT];
    int pages[2];
    int npages;
    int reps;
    uint64_t min_ops;
    int use_perf;
    const char *csv_path;
    uint64_t seed;
    uint64_t flush_mb;
} Options;

typedef struct Variant {
    const BenchImpl *impl;
    int j; /* -1 for structures without j */
    char label[32];
} Variant;

typedef struct Result {
    int mode, variant, pattern, op;
    uint64_t keys;
    uint64_t ops_per_sample;
    int samples;
    double ns_op;
    double metric[PC_COUNT]; /* per op, except PC_PAGE_FAULTS = per measured run */
    int valid[PC_COUNT];
    int ok;
} Result;

static Options opt;
static Perf perf;
static Variant variants[MAX_VARIANTS];
static int nvariants;
static Result *results;
static size_t nresults, cap_results;

/* ---------------------------------------------------------------- options */

static void usage(FILE *f)
{
    fprintf(f,
            "Usage: vdc_bench [options]\n"
            "  --impl LIST       vdc,hash,skiplist                          (default vdc)\n"
            "  --j LIST          unreversed low key bits for vdc            (default 6,8,11,15,20)\n"
            "  --keys LIST       key counts, K = 1e3, M = 1e6, max 1048576  (default 1K,10K,100K,500K,1M)\n"
            "  --patterns LIST   seq,dense_rand,rand                        (default all)\n"
            "  --ops LIST        insert,get_hit,get_miss,get_batch,update,delete (default all)\n"
            "  --pages LIST      base,hugetlb                               (default base)\n"
            "  --reps N          repetitions per cell and counter set       (default 5)\n"
            "  --min-ops N       minimum ops per read/update sample         (default 1M)\n"
            "  --no-perf         do not use hardware counters\n"
            "  --flush-mb N      write N MiB between repetitions to evict caches (default 0)\n"
            "  --csv FILE        append all results to FILE as CSV\n"
            "  --seed N          RNG seed                                   (default 42)\n"
            "The markdown report goes to stdout, progress to stderr.\n");
}

static void set_defaults(Options *o)
{
    memset(o, 0, sizeof(*o));
    o->impls[0] = 0;
    o->nimpls = 1;
    const uint64_t js[] = {6, 8, 11, 15, 20};
    o->nj = 5;
    memcpy(o->js, js, sizeof(js));
    const uint64_t keys[] = {1000, 10000, 100000, 500000, 1000000};
    o->nkeys = 5;
    memcpy(o->keys, keys, sizeof(keys));
    for (int p = 0; p < PAT_COUNT; p++)
        o->patterns[p] = p;
    o->npatterns = PAT_COUNT;
    for (int op = 0; op < OP_COUNT; op++)
        o->report_op[op] = 1;
    o->pages[0] = VDC_PAGES_BASE;
    o->npages = 1;
    o->reps = 5;
    o->min_ops = 1000000;
    o->use_perf = 1;
    o->seed = 42;
}

static int parse_args(Options *o, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        int n = 0;

        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            exit(0);
        } else if (!strcmp(a, "--no-perf")) {
            o->use_perf = 0;
            continue;
        }

        if (v == NULL) {
            fprintf(stderr, "error: unknown option or missing value: %s\n", a);
            return -1;
        }
        i++;

        if (!strcmp(a, "--impl")) {
            n = o->nimpls = bench_parse_name_list(v, impl_names, NUM_IMPLS, o->impls, NUM_IMPLS);
        } else if (!strcmp(a, "--j")) {
            n = o->nj = bench_parse_u64_list(v, o->js, MAX_LIST);
        } else if (!strcmp(a, "--keys")) {
            n = o->nkeys = bench_parse_u64_list(v, o->keys, MAX_LIST);
        } else if (!strcmp(a, "--patterns")) {
            n = o->npatterns = bench_parse_name_list(v, bench_pattern_names, PAT_COUNT, o->patterns, PAT_COUNT);
        } else if (!strcmp(a, "--ops")) {
            int tmp[OP_COUNT];
            n = bench_parse_name_list(v, bench_op_names, OP_COUNT, tmp, OP_COUNT);
            if (n > 0) {
                memset(o->report_op, 0, sizeof(o->report_op));
                for (int k = 0; k < n; k++)
                    o->report_op[tmp[k]] = 1;
            }
        } else if (!strcmp(a, "--pages")) {
            n = o->npages = bench_parse_name_list(v, page_names, 2, o->pages, 2);
        } else if (!strcmp(a, "--reps")) {
            uint64_t r;
            n = bench_parse_u64_list(v, &r, 1);
            o->reps = (int)r;
        } else if (!strcmp(a, "--min-ops")) {
            n = bench_parse_u64_list(v, &o->min_ops, 1);
        } else if (!strcmp(a, "--flush-mb")) {
            n = bench_parse_u64_list(v, &o->flush_mb, 1);
        } else if (!strcmp(a, "--seed")) {
            n = bench_parse_u64_list(v, &o->seed, 1);
        } else if (!strcmp(a, "--csv")) {
            o->csv_path = v;
            n = 1;
        } else {
            fprintf(stderr, "error: unknown option %s\n", a);
            return -1;
        }
        if (n <= 0) {
            fprintf(stderr, "error: bad value for %s: %s\n", a, v);
            return -1;
        }
    }

    if (o->reps < 1 || o->reps > MAX_REPS) {
        fprintf(stderr, "error: --reps must be 1..%d\n", MAX_REPS);
        return -1;
    }
    for (int i = 0; i < o->nkeys; i++) {
        if (o->keys[i] == 0 || o->keys[i] > VDC_NUM_SLOTS) {
            fprintf(stderr, "error: key counts must be 1..%u (one memtable)\n", VDC_NUM_SLOTS);
            return -1;
        }
    }
    for (int i = 0; i < o->nj; i++) {
        if (o->js[i] > VDC_KEY_BITS) {
            fprintf(stderr, "error: j must be 0..%u\n", VDC_KEY_BITS);
            return -1;
        }
    }
    if (o->min_ops == 0)
        o->min_ops = 1;
    return 0;
}

static void build_variants(const Options *o)
{
    nvariants = 0;
    for (int i = 0; i < o->nimpls; i++) {
        const BenchImpl *im = impl_table[o->impls[i]];
        if (im->uses_j) {
            for (int k = 0; k < o->nj && nvariants < MAX_VARIANTS; k++) {
                Variant *v = &variants[nvariants++];
                v->impl = im;
                v->j = (int)o->js[k];
                snprintf(v->label, sizeof(v->label), o->nimpls == 1 ? "j=%d" : "vdc j=%d", v->j);
            }
        } else if (nvariants < MAX_VARIANTS) {
            Variant *v = &variants[nvariants++];
            v->impl = im;
            v->j = -1;
            snprintf(v->label, sizeof(v->label), "%s", im->name);
        }
    }
}

/* ---------------------------------------------------------------- keys */

static void make_keys(int pat, size_t n, uint64_t seed, uint32_t *ins, uint32_t *acc, uint32_t *miss,
                      uint32_t *scratch, BenchKeys *k)
{
    BenchRng r = {seed * 0x9E3779B97F4A7C15ull + (uint64_t)n * 1000003ull + (uint64_t)pat};
    const size_t dom = VDC_NUM_SLOTS;
    size_t m = dom - n;
    if (m > n)
        m = n;

    if (pat == PAT_SEQ) {
        for (size_t i = 0; i < n; i++)
            ins[i] = acc[i] = (uint32_t)i;
        for (size_t i = 0; i < m; i++)
            miss[i] = (uint32_t)(n + i);
    } else if (pat == PAT_DENSE_RAND) {
        for (size_t i = 0; i < n; i++)
            ins[i] = (uint32_t)i;
        bench_shuffle_u32(ins, n, &r);
        memcpy(acc, ins, n * sizeof(*acc));
        bench_shuffle_u32(acc, n, &r);
        for (size_t i = 0; i < dom - n; i++)
            scratch[i] = (uint32_t)(n + i);
        bench_shuffle_u32(scratch, dom - n, &r);
        memcpy(miss, scratch, m * sizeof(*miss));
    } else {
        for (size_t i = 0; i < dom; i++)
            scratch[i] = (uint32_t)i;
        bench_shuffle_u32(scratch, dom, &r);
        memcpy(ins, scratch, n * sizeof(*ins));
        memcpy(acc, ins, n * sizeof(*acc));
        bench_shuffle_u32(acc, n, &r);
        memcpy(miss, scratch + n, m * sizeof(*miss));
    }
    k->insert = ins;
    k->access = acc;
    k->n = n;
    k->miss = miss;
    k->n_miss = m;
}

/* ---------------------------------------------------------------- measuring */

static void add_result(const Result *r)
{
    if (nresults == cap_results) {
        cap_results = cap_results ? cap_results * 2 : 1024;
        Result *nr = realloc(results, cap_results * sizeof(*results));
        if (nr == NULL) {
            fprintf(stderr, "error: out of memory\n");
            exit(2);
        }
        results = nr;
    }
    results[nresults++] = *r;
}

static int op_runs(const BenchImpl *im, const BenchKeys *k, int op)
{
    if (op == OP_INSERT)
        return 1; /* always needed to fill the table */
    if (!opt.report_op[op])
        return 0;
    if (op == OP_GET_BATCH && !im->has_batch)
        return 0;
    if (op == OP_GET_MISS && k->n_miss == 0)
        return 0;
    return 1;
}

static double tns[OP_COUNT][MAX_SAMPLES];
static double ctr[OP_COUNT][PC_COUNT][MAX_SAMPLES];

static void run_cell(const BenchImpl *im, void *t, int mode, int vi, int pat, const BenchKeys *k,
                     unsigned char *flush_buf, size_t flush_bytes)
{
    int nt[OP_COUNT] = {0};
    int nc[OP_COUNT][PC_COUNT];
    uint64_t ops_per[OP_COUNT] = {0};
    int ok[OP_COUNT], ran[OP_COUNT] = {0};
    memset(nc, 0, sizeof(nc));
    for (int op = 0; op < OP_COUNT; op++)
        ok[op] = 1;

    const int nsets = perf.nsets > 0 ? perf.nsets : 1;
    for (int rep = 0; rep < opt.reps; rep++) {
        for (int s = 0; s < nsets; s++) {
            im->clear(t);
            if (flush_buf != NULL)
                bench_flush_caches(flush_buf, flush_bytes);

            for (int op = 0; op < OP_COUNT; op++) {
                if (!op_runs(im, k, op))
                    continue;
                const size_t base = (op == OP_GET_MISS) ? k->n_miss : k->n;
                size_t passes = 1;
                if (op == OP_GET_HIT || op == OP_GET_MISS || op == OP_GET_BATCH || op == OP_UPDATE)
                    passes = (size_t)((opt.min_ops + base - 1) / base);

                PhaseResult pr;
                PerfReading rd;
                if (perf.nsets > 0)
                    perf_start(&perf, s);
                const uint64_t t0 = bench_now_ns();
                im->run_phase(t, (BenchOp)op, k, passes, &pr);
                const uint64_t t1 = bench_now_ns();
                if (perf.nsets > 0)
                    perf_stop(&perf, s, &rd);
                else
                    memset(&rd, 0, sizeof(rd));

                if (pr.ops == 0)
                    continue;
                tns[op][nt[op]++] = (double)(t1 - t0) / (double)pr.ops;
                for (int ev = 0; ev < PC_COUNT; ev++) {
                    if (!rd.valid[ev])
                        continue;
                    ctr[op][ev][nc[op][ev]++] = (ev == PC_PAGE_FAULTS) ? rd.value[ev] : rd.value[ev] / (double)pr.ops;
                }
                ok[op] &= pr.ok;
                ops_per[op] = pr.ops;
                ran[op] = 1;
            }
        }
    }

    for (int op = 0; op < OP_COUNT; op++) {
        if (!ran[op] || !opt.report_op[op])
            continue;
        Result r;
        memset(&r, 0, sizeof(r));
        r.mode = mode;
        r.variant = vi;
        r.pattern = pat;
        r.op = op;
        r.keys = k->n;
        r.ops_per_sample = ops_per[op];
        r.samples = nt[op];
        r.ns_op = bench_median(tns[op], (size_t)nt[op]);
        for (int ev = 0; ev < PC_COUNT; ev++) {
            if (nc[op][ev] > 0) {
                r.metric[ev] = bench_median(ctr[op][ev], (size_t)nc[op][ev]);
                r.valid[ev] = 1;
            }
        }
        r.ok = ok[op];
        add_result(&r);
    }
}

/* ---------------------------------------------------------------- lookups / formatting */

static const Result *find_result(int mode, int var, int pat, int op, uint64_t keys)
{
    for (size_t i = 0; i < nresults; i++) {
        const Result *r = &results[i];
        if (r->mode == mode && r->variant == var && r->pattern == pat && r->op == op && r->keys == keys)
            return r;
    }
    return NULL;
}

static int get_metric(const Result *r, int m, double *out)
{
    if (r == NULL)
        return 0;
    if (m == M_NS) {
        *out = r->ns_op;
        return 1;
    }
    if (m == M_DTLB) {
        if (!r->valid[PC_DTLB_LOAD_MISS] && !r->valid[PC_DTLB_STORE_MISS])
            return 0;
        *out = (r->valid[PC_DTLB_LOAD_MISS] ? r->metric[PC_DTLB_LOAD_MISS] : 0.0) +
               (r->valid[PC_DTLB_STORE_MISS] ? r->metric[PC_DTLB_STORE_MISS] : 0.0);
        return 1;
    }
    if (!r->valid[m])
        return 0;
    *out = r->metric[m];
    return 1;
}

static const char *metric_title(int m)
{
    switch (m) {
    case M_NS: return "Time per operation (ns/op)";
    case M_DTLB: return "dTLB misses per operation (load + store)";
    case PC_CYCLES: return "Cycles per operation";
    case PC_INSTRUCTIONS: return "Instructions per operation";
    case PC_L1D_LOAD_MISS: return "L1D load misses per operation";
    case PC_LLC_MISS: return "LLC misses per operation";
    case PC_DTLB_LOAD_MISS: return "dTLB load misses per operation";
    case PC_DTLB_STORE_MISS: return "dTLB store misses per operation";
    case PC_PAGE_FAULTS: return "Page faults per measured run";
    default: return "?";
    }
}

static void fmt_keys(char *b, size_t n, uint64_t k)
{
    if (k >= 1000000 && k % 1000000 == 0)
        snprintf(b, n, "%lluM", (unsigned long long)(k / 1000000));
    else if (k >= 1000 && k % 1000 == 0)
        snprintf(b, n, "%lluK", (unsigned long long)(k / 1000));
    else
        snprintf(b, n, "%llu", (unsigned long long)k);
}

static void fmt_value(char *b, size_t n, int m, double v)
{
    if (m == M_NS || m == PC_INSTRUCTIONS || m == PC_CYCLES)
        snprintf(b, n, "%.1f", v);
    else if (m == PC_PAGE_FAULTS)
        snprintf(b, n, "%.0f", v);
    else
        snprintf(b, n, "%.3f", v);
}

static void fmt_where(char *b, size_t n, const Result *r)
{
    char kb[16];
    fmt_keys(kb, sizeof(kb), r->keys);
    snprintf(b, n, "%s / %s / %s / %s / %s keys", page_names[r->mode], variants[r->variant].label,
             bench_pattern_names[r->pattern], bench_op_names[r->op], kb);
}

static uint64_t largest_keys(void)
{
    uint64_t m = 0;
    for (int i = 0; i < opt.nkeys; i++)
        if (opt.keys[i] > m)
            m = opt.keys[i];
    return m;
}

static int has_any(int mode, int pat, int op)
{
    for (size_t i = 0; i < nresults; i++)
        if (results[i].mode == mode && results[i].pattern == pat && results[i].op == op)
            return 1;
    return 0;
}

/* ---------------------------------------------------------------- report */

static void print_pivot(FILE *f, int mode, int pat, int op, int m)
{
    int any = 0;
    double v;
    for (int ki = 0; ki < opt.nkeys && !any; ki++)
        for (int vi = 0; vi < nvariants && !any; vi++)
            any = get_metric(find_result(mode, vi, pat, op, opt.keys[ki]), m, &v);
    if (!any)
        return;

    fprintf(f, "\n%s\n\n| keys |", metric_title(m));
    for (int vi = 0; vi < nvariants; vi++)
        fprintf(f, " %s |", variants[vi].label);
    fprintf(f, "\n|---|");
    for (int vi = 0; vi < nvariants; vi++)
        fprintf(f, "---:|");
    fprintf(f, "\n");

    for (int ki = 0; ki < opt.nkeys; ki++) {
        char kb[16], vb[32];
        fmt_keys(kb, sizeof(kb), opt.keys[ki]);
        fprintf(f, "| %s |", kb);
        for (int vi = 0; vi < nvariants; vi++) {
            if (get_metric(find_result(mode, vi, pat, op, opt.keys[ki]), m, &v)) {
                fmt_value(vb, sizeof(vb), m, v);
                fprintf(f, " %s |", vb);
            } else {
                fprintf(f, " - |");
            }
        }
        fprintf(f, "\n");
    }
}

static void print_config(FILE *f)
{
    fprintf(f, "\n## Configuration\n\n| Setting | Value |\n|---|---|\n");

    fprintf(f, "| Variants |");
    for (int vi = 0; vi < nvariants; vi++)
        fprintf(f, "%s %s", vi ? "," : "", variants[vi].label);
    fprintf(f, " |\n| Key counts |");
    for (int i = 0; i < opt.nkeys; i++) {
        char kb[16];
        fmt_keys(kb, sizeof(kb), opt.keys[i]);
        fprintf(f, "%s %s", i ? "," : "", kb);
    }
    fprintf(f, " (K = 1,000, M = 1,000,000; key domain 0 .. 2^20-1) |\n");

    fprintf(f, "| Patterns |");
    for (int i = 0; i < opt.npatterns; i++)
        fprintf(f, "%s %s", i ? "," : "", bench_pattern_names[opt.patterns[i]]);
    fprintf(f, " |\n| Operations |");
    for (int op = 0, first = 1; op < OP_COUNT; op++)
        if (opt.report_op[op]) {
            fprintf(f, "%s %s", first ? "" : ",", bench_op_names[op]);
            first = 0;
        }
    fprintf(f, " |\n| Page modes |");
    for (int i = 0; i < opt.npages; i++)
        fprintf(f, "%s %s", i ? "," : "", page_names[opt.pages[i]]);
    fprintf(f, " |\n");
    fprintf(f, "| Repetitions | %d per counter set, %d counter set(s); values are medians |\n", opt.reps,
            perf.nsets > 0 ? perf.nsets : 1);
    fprintf(f, "| Min ops per read/update sample | %llu |\n", (unsigned long long)opt.min_ops);
    fprintf(f, "| Value size | %u B (inline in the Node) |\n", BENCH_VALUE_BYTES);
    fprintf(f, "| Memory | fully committed before measuring (populate) |\n");
    fprintf(f, "| Cache flush between repetitions | %llu MiB |\n", (unsigned long long)opt.flush_mb);
    fprintf(f, "| Seed | %llu |\n", (unsigned long long)opt.seed);

    fprintf(f, "| Hardware counters |");
    if (perf.nsets == 0) {
        fprintf(f, " unavailable (%s) |\n",
                opt.use_perf ? "no permission, VM or non-Linux; see scripts/perf_counters.sh" : "disabled");
    } else {
        int first = 1;
        for (int ev = 0; ev < PC_COUNT; ev++)
            if (perf.available[ev]) {
                fprintf(f, "%s %s", first ? "" : ",", perf_event_name(ev));
                first = 0;
            }
        fprintf(f, " |\n");
    }

    fprintf(f, "\nPatterns: **seq** = keys 0..N-1 in ascending order; **dense_rand** = keys 0..N-1 in random "
               "order; **rand** = N random keys from the whole domain in random order.\n");
    fprintf(f, "Operations: **insert** = N new keys into an empty table; **get_hit** / **get_miss** = lookups of "
               "present / never-written keys; **get_batch** = get_hit through the prefetching batch API; "
               "**update** = overwrite present keys; **delete** = tombstone present keys.\n");
}

static void print_checks(FILE *f)
{
    char where[160];
    double v;

    fprintf(f, "\n## Checks\n\n| Check | Target | Result | Worst value | Where |\n|---|---|---|---|---|\n");

    /* Correctness. */
    const Result *bad = NULL;
    for (size_t i = 0; i < nresults && bad == NULL; i++)
        if (!results[i].ok)
            bad = &results[i];
    if (bad != NULL)
        fmt_where(where, sizeof(where), bad);
    fprintf(f, "| Correctness: every operation returned the expected status | all ok | %s | %s | %s |\n",
            bad ? "❌ FAIL" : "✅ PASS", bad ? "failed" : "-", bad ? where : "-");

    /* O(1): instructions per op flat across key counts, per structure. */
    for (int ii = 0; ii < opt.nimpls; ii++) {
        const BenchImpl *im = impl_table[opt.impls[ii]];
        double worst = 0.0;
        int found = 0;
        for (int mi = 0; mi < opt.npages; mi++)
            for (int vi = 0; vi < nvariants; vi++) {
                if (variants[vi].impl != im)
                    continue;
                for (int pi = 0; pi < opt.npatterns; pi++)
                    for (int op = 0; op < OP_COUNT; op++) {
                        double lo = 1e300, hi = 0.0;
                        int cnt = 0;
                        const Result *rw = NULL;
                        for (int ki = 0; ki < opt.nkeys; ki++) {
                            const Result *r = find_result(opt.pages[mi], vi, opt.patterns[pi], op, opt.keys[ki]);
                            if (get_metric(r, PC_INSTRUCTIONS, &v) && v > 0) {
                                if (v < lo)
                                    lo = v;
                                if (v > hi) {
                                    hi = v;
                                    rw = r;
                                }
                                cnt++;
                            }
                        }
                        if (cnt >= 2 && hi / lo > worst) {
                            worst = hi / lo;
                            fmt_where(where, sizeof(where), rw);
                            found = 1;
                        }
                    }
            }
        if (found)
            fprintf(f, "| O(1): instructions/op flat from smallest to largest key count (%s) | max/min ≤ %.2f | %s | "
                       "%.2f | %s |\n",
                    im->name, CHECK_O1_MAX_RATIO, worst <= CHECK_O1_MAX_RATIO ? "✅ PASS" : "❌ FAIL", worst, where);
        else
            fprintf(f, "| O(1): instructions/op flat across key counts (%s) | max/min ≤ %.2f | n/a | needs counters "
                       "and ≥ 2 key counts | - |\n",
                    im->name, CHECK_O1_MAX_RATIO);
    }

    /* Cache: VDC touches about one line per op. */
    {
        double worst = -1.0;
        for (size_t i = 0; i < nresults; i++)
            if (variants[results[i].variant].impl == &bench_impl_vdc && get_metric(&results[i], PC_LLC_MISS, &v) &&
                v > worst) {
                worst = v;
                fmt_where(where, sizeof(where), &results[i]);
            }
        if (worst >= 0)
            fprintf(f, "| Cache: LLC misses/op for vdc (one 64 B Node per op) | ≤ %.2f | %s | %.3f | %s |\n",
                    CHECK_LLC_MAX, worst <= CHECK_LLC_MAX ? "✅ PASS" : "❌ FAIL", worst, where);
        else
            fprintf(f, "| Cache: LLC misses/op for vdc | ≤ %.2f | n/a | needs counters | - |\n", CHECK_LLC_MAX);
    }

    /* TLB: with 2 MiB pages the 64 MiB space needs only 32 TLB entries. */
    {
        double worst = -1.0;
        for (size_t i = 0; i < nresults; i++)
            if (results[i].mode == VDC_PAGES_HUGETLB && variants[results[i].variant].impl == &bench_impl_vdc &&
                get_metric(&results[i], M_DTLB, &v) && v > worst) {
                worst = v;
                fmt_where(where, sizeof(where), &results[i]);
            }
        if (worst >= 0)
            fprintf(f, "| TLB: dTLB misses/op for vdc on hugetlb pages | ≤ %.2f | %s | %.3f | %s |\n",
                    CHECK_DTLB_HUGE_MAX, worst <= CHECK_DTLB_HUGE_MAX ? "✅ PASS" : "❌ FAIL", worst, where);
        else
            fprintf(f, "| TLB: dTLB misses/op for vdc on hugetlb pages | ≤ %.2f | n/a | needs hugetlb run and "
                       "counters | - |\n",
                    CHECK_DTLB_HUGE_MAX);
    }

    /* Page faults: memory is populated, so measured runs must not fault. */
    {
        double worst = -1.0;
        for (size_t i = 0; i < nresults; i++)
            if (get_metric(&results[i], PC_PAGE_FAULTS, &v) && v > worst) {
                worst = v;
                fmt_where(where, sizeof(where), &results[i]);
            }
        if (worst >= 0)
            fprintf(f, "| Page faults during measured runs (median) | 0 | %s | %.0f | %s |\n",
                    worst <= 0.0 ? "✅ PASS" : "❌ FAIL", worst, worst > 0.0 ? where : "-");
        else
            fprintf(f, "| Page faults during measured runs | 0 | n/a | needs counters | - |\n");
    }

    fprintf(f, "\nTime per operation is expected to grow with the key count (the working set moves from L1 to "
               "DRAM); instructions per operation must not. LLC misses can exceed 1.0 when the hardware "
               "adjacent-line prefetcher fetches the neighbouring line (accepted cost, P17).\n");
}

static void print_o1_detail(FILE *f)
{
    double v;
    fprintf(f, "\n## O(1) detail: max/min instructions per op across key counts\n\n"
               "1.00 = identical work at every key count. ✅ ≤ %.2f.\n",
            CHECK_O1_MAX_RATIO);
    for (int mi = 0; mi < opt.npages; mi++)
        for (int pi = 0; pi < opt.npatterns; pi++) {
            const int mode = opt.pages[mi], pat = opt.patterns[pi];
            fprintf(f, "\n**%s pages, %s**\n\n| op |", page_names[mode], bench_pattern_names[pat]);
            for (int vi = 0; vi < nvariants; vi++)
                fprintf(f, " %s |", variants[vi].label);
            fprintf(f, "\n|---|");
            for (int vi = 0; vi < nvariants; vi++)
                fprintf(f, "---:|");
            fprintf(f, "\n");
            for (int op = 0; op < OP_COUNT; op++) {
                if (!has_any(mode, pat, op))
                    continue;
                fprintf(f, "| %s |", bench_op_names[op]);
                for (int vi = 0; vi < nvariants; vi++) {
                    double lo = 1e300, hi = 0.0;
                    int cnt = 0;
                    for (int ki = 0; ki < opt.nkeys; ki++)
                        if (get_metric(find_result(mode, vi, pat, op, opt.keys[ki]), PC_INSTRUCTIONS, &v) && v > 0) {
                            lo = v < lo ? v : lo;
                            hi = v > hi ? v : hi;
                            cnt++;
                        }
                    if (cnt >= 2)
                        fprintf(f, " %.2f %s |", hi / lo, hi / lo <= CHECK_O1_MAX_RATIO ? "✅" : "❌");
                    else
                        fprintf(f, " - |");
                }
                fprintf(f, "\n");
            }
        }
}

static void print_hugepages(FILE *f)
{
    if (opt.npages < 2)
        return;
    const uint64_t kmax = largest_keys();
    char kb[16];
    fmt_keys(kb, sizeof(kb), kmax);
    double a, b;

    fprintf(f, "\n## Huge pages vs base pages (%s keys)\n\nCell = base → hugetlb.\n", kb);
    for (int pi = 0; pi < opt.npatterns; pi++) {
        const int pat = opt.patterns[pi];
        for (int which = 0; which < 2; which++) {
            const int m = which == 0 ? M_DTLB : M_NS;
            fprintf(f, "\n**%s: %s**\n\n| op |", bench_pattern_names[pat], metric_title(m));
            for (int vi = 0; vi < nvariants; vi++)
                fprintf(f, " %s |", variants[vi].label);
            fprintf(f, "\n|---|");
            for (int vi = 0; vi < nvariants; vi++)
                fprintf(f, "---:|");
            fprintf(f, "\n");
            for (int op = 0; op < OP_COUNT; op++) {
                if (!has_any(VDC_PAGES_BASE, pat, op))
                    continue;
                fprintf(f, "| %s |", bench_op_names[op]);
                for (int vi = 0; vi < nvariants; vi++) {
                    const int ha = get_metric(find_result(VDC_PAGES_BASE, vi, pat, op, kmax), m, &a);
                    const int hb = get_metric(find_result(VDC_PAGES_HUGETLB, vi, pat, op, kmax), m, &b);
                    if (ha && hb && m == M_NS)
                        fprintf(f, " %.1f → %.1f (%+.0f%%) |", a, b, a > 0 ? (b - a) / a * 100.0 : 0.0);
                    else if (ha && hb)
                        fprintf(f, " %.3f → %.3f |", a, b);
                    else
                        fprintf(f, " - |");
                }
                fprintf(f, "\n");
            }
        }
    }
}

static void print_best(FILE *f)
{
    const uint64_t kmax = largest_keys();
    char kb[16];
    fmt_keys(kb, sizeof(kb), kmax);
    double v;

    fprintf(f, "\n## Fastest variant per workload (%s keys, ns/op)\n", kb);
    for (int mi = 0; mi < opt.npages; mi++) {
        const int mode = opt.pages[mi];
        fprintf(f, "\n**%s pages**\n\n| op |", page_names[mode]);
        for (int pi = 0; pi < opt.npatterns; pi++)
            fprintf(f, " %s |", bench_pattern_names[opt.patterns[pi]]);
        fprintf(f, "\n|---|");
        for (int pi = 0; pi < opt.npatterns; pi++)
            fprintf(f, "---|");
        fprintf(f, "\n");
        for (int op = 0; op < OP_COUNT; op++) {
            if (!opt.report_op[op])
                continue;
            fprintf(f, "| %s |", bench_op_names[op]);
            for (int pi = 0; pi < opt.npatterns; pi++) {
                int best = -1;
                double bv = 0.0;
                for (int vi = 0; vi < nvariants; vi++)
                    if (get_metric(find_result(mode, vi, opt.patterns[pi], op, kmax), M_NS, &v) &&
                        (best < 0 || v < bv)) {
                        best = vi;
                        bv = v;
                    }
                if (best >= 0)
                    fprintf(f, " %s (%.1f) |", variants[best].label, bv);
                else
                    fprintf(f, " - |");
            }
            fprintf(f, "\n");
        }
    }
}

static void print_workloads(FILE *f)
{
    static const int metrics[] = {M_NS, PC_INSTRUCTIONS, PC_L1D_LOAD_MISS, PC_LLC_MISS, M_DTLB};
    fprintf(f, "\n## Results by workload\n\nRows = key count, columns = variant. All values are per operation "
               "(medians). The CSV file has every metric, including cycles and page faults.\n");
    for (int mi = 0; mi < opt.npages; mi++)
        for (int pi = 0; pi < opt.npatterns; pi++)
            for (int op = 0; op < OP_COUNT; op++) {
                const int mode = opt.pages[mi], pat = opt.patterns[pi];
                if (!has_any(mode, pat, op))
                    continue;
                fprintf(f, "\n### %s pages / %s / %s\n", page_names[mode], bench_pattern_names[pat],
                        bench_op_names[op]);
                for (size_t m = 0; m < sizeof(metrics) / sizeof(metrics[0]); m++)
                    print_pivot(f, mode, pat, op, metrics[m]);
            }
}

static void print_report(FILE *f)
{
    fprintf(f, "# VDC memtable benchmark report\n\n## System\n\n");
    bench_print_system_info(f);
    print_config(f);
    print_checks(f);
    print_o1_detail(f);
    print_hugepages(f);
    print_best(f);
    print_workloads(f);
}

static void write_csv(const char *path)
{
    FILE *f = fopen(path, "a");
    if (f == NULL) {
        fprintf(stderr, "warning: cannot open %s: %s\n", path, strerror(errno));
        return;
    }
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0)
        fprintf(f, "impl,j,variant,page_mode,pattern,op,keys,ops_per_sample,samples,ns_op,cycles_op,instr_op,"
                   "l1d_load_miss_op,llc_miss_op,dtlb_load_miss_op,dtlb_store_miss_op,page_faults,ok\n");
    static const int cols[] = {PC_CYCLES,         PC_INSTRUCTIONS,    PC_L1D_LOAD_MISS, PC_LLC_MISS,
                               PC_DTLB_LOAD_MISS, PC_DTLB_STORE_MISS, PC_PAGE_FAULTS};
    for (size_t i = 0; i < nresults; i++) {
        const Result *r = &results[i];
        const Variant *v = &variants[r->variant];
        fprintf(f, "%s,", v->impl->name);
        if (v->j >= 0)
            fprintf(f, "%d", v->j);
        fprintf(f, ",%s,%s,%s,%s,%llu,%llu,%d,%.3f", v->label, page_names[r->mode], bench_pattern_names[r->pattern],
                bench_op_names[r->op], (unsigned long long)r->keys, (unsigned long long)r->ops_per_sample, r->samples,
                r->ns_op);
        for (size_t c = 0; c < sizeof(cols) / sizeof(cols[0]); c++) {
            if (r->valid[cols[c]])
                fprintf(f, ",%.4f", r->metric[cols[c]]);
            else
                fprintf(f, ",");
        }
        fprintf(f, ",%d\n", r->ok);
    }
    fclose(f);
}

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    memset(bench_value_a, 'A', sizeof(bench_value_a));
    memset(bench_value_b, 'B', sizeof(bench_value_b));

    set_defaults(&opt);
    if (parse_args(&opt, argc, argv) != 0) {
        usage(stderr);
        return 2;
    }

    /* Fail loudly if hugetlb was asked for but the pool is not there (P15 decision). */
    for (int i = 0; i < opt.npages; i++) {
        if (opt.pages[i] != VDC_PAGES_HUGETLB)
            continue;
        void *p = vdc_map(VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB, 1);
        if (p == NULL) {
            fprintf(stderr,
                    "ERROR: cannot map a 2 MiB hugetlb page (%s).\n"
                    "       Reserve the pool first: sudo scripts/setup_hugepages.sh\n",
                    strerror(errno));
            return 2;
        }
        vdc_unmap(p, VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB);
    }

    perf_init(&perf, opt.use_perf);
    if (opt.use_perf && perf.nsets == 0)
        fprintf(stderr, "warning: hardware counters unavailable; reporting timings only "
                        "(see scripts/perf_counters.sh)\n");

    build_variants(&opt);

    uint32_t *ins = malloc(VDC_NUM_SLOTS * sizeof(uint32_t));
    uint32_t *acc = malloc(VDC_NUM_SLOTS * sizeof(uint32_t));
    uint32_t *miss = malloc(VDC_NUM_SLOTS * sizeof(uint32_t));
    uint32_t *scratch = malloc(VDC_NUM_SLOTS * sizeof(uint32_t));
    const size_t flush_bytes = (size_t)opt.flush_mb << 20;
    unsigned char *flush_buf = flush_bytes ? calloc(1, flush_bytes) : NULL;
    if (!ins || !acc || !miss || !scratch || (flush_bytes && !flush_buf)) {
        fprintf(stderr, "error: out of memory\n");
        return 2;
    }

    BenchImplConfig cfg;
    cfg.max_keys = (size_t)largest_keys();

    for (int mi = 0; mi < opt.npages; mi++) {
        const int mode = opt.pages[mi];
        for (int vi = 0; vi < nvariants; vi++) {
            const BenchImpl *im = variants[vi].impl;
            cfg.j = variants[vi].j >= 0 ? (unsigned)variants[vi].j : 0;
            cfg.page_mode = (VdcPageMode)mode;

            const uint64_t tc0 = bench_now_ns();
            void *t = im->create(&cfg);
            if (t == NULL) {
                fprintf(stderr,
                        "ERROR: could not create %s on %s pages (%s).\n"
                        "       For hugetlb, the pool must hold the whole structure "
                        "(vdc 32 pages, hash up to 64, skiplist up to 48 for 1M keys).\n",
                        variants[vi].label, page_names[mode], strerror(errno));
                return 2;
            }
            fprintf(stderr, "[%s] %-12s created in %.1f ms\n", page_names[mode], variants[vi].label,
                    (double)(bench_now_ns() - tc0) / 1e6);

            for (int ki = 0; ki < opt.nkeys; ki++) {
                for (int pi = 0; pi < opt.npatterns; pi++) {
                    BenchKeys k;
                    const uint64_t t0 = bench_now_ns();
                    make_keys(opt.patterns[pi], (size_t)opt.keys[ki], opt.seed, ins, acc, miss, scratch, &k);
                    run_cell(im, t, mode, vi, opt.patterns[pi], &k, flush_buf, flush_bytes);
                    char kb[16];
                    fmt_keys(kb, sizeof(kb), opt.keys[ki]);
                    fprintf(stderr, "[%s] %-12s %5s keys %-10s %.2f s\n", page_names[mode], variants[vi].label, kb,
                            bench_pattern_names[opt.patterns[pi]], (double)(bench_now_ns() - t0) / 1e9);
                }
            }
            im->destroy(t);
        }
    }

    print_report(stdout);
    if (opt.csv_path != NULL)
        write_csv(opt.csv_path);

    int failed = 0;
    for (size_t i = 0; i < nresults; i++)
        failed |= !results[i].ok;
    if (failed)
        fprintf(stderr, "ERROR: some operations returned unexpected results (see the Checks table)\n");

    perf_close(&perf);
    free(ins);
    free(acc);
    free(miss);
    free(scratch);
    free(flush_buf);
    free(results);
    return failed ? 1 : 0;
}
