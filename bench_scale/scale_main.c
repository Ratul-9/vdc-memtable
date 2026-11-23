/*
 * dat_scale - scale experiments of the direct-address memtable (10M - 100M keys)
 * under a hard memory budget.
 *
 *   dat_scale --experiment N [options]      run experiment N (1-5), markdown report on stdout
 *   dat_scale --experiment N --plan         only print the memory plan (nothing is allocated)
 *   dat_scale --experiment 4 --hugepages-needed   print how many 2 MiB pages experiment 4 needs
 *
 * Experiments
 *   1  flat array at scale (reference)          flat;                         dense; seq + rand
 *   2  cost of the row table                    flat, rows-4K/64K/2M;         dense; seq + rand
 *   3  key density: what on-demand rows save    flat, rows-4K/64K/2M;         dense, clustered, sparse; rand
 *   4  huge pages at scale (hugetlb)            flat, rows-64K, rows-2M;      dense; seq + rand
 *   5  baselines at scale                       hash table, skip list;       dense; seq + rand
 *
 * Memory rules
 *   - one structure exists at a time; it is destroyed before the next is created;
 *   - before creating it, its memory is computed; if program + structure is
 *     above the budget (default 9 GiB, at most 10 GiB) it is skipped and the
 *     report says how much it would have needed;
 *   - after creating it, the real memory (VmRSS + HugetlbPages) is measured;
 *     above 10 GiB the program stops at once;
 *   - keys are computed when needed (dat_keys.h): no key arrays.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat/dat_node.h"
#include "scale.h"
#include "vdc/memtable.h"

#define MAX_LIST 16
#define MAX_REPS 20
#define MAX_STRUCTS 8

static const char *const order_names[ORD_COUNT] = {"seq", "rand"};
static const char *const op_names[PH_COUNT] = {"insert", "get_hit", "get_miss", "update"};
static const char *const page_names[2] = {"base", "hugetlb"};

typedef struct ExpDef {
    int id;
    const char *title;
    const char *question;
    int kinds[MAX_STRUCTS];
    unsigned row_bits[MAX_STRUCTS];
    int nstructs;
    int patterns[DAT_PATTERN_COUNT];
    int npatterns;
    int orders[ORD_COUNT];
    int norders;
    unsigned ops_mask;
    VdcPageMode mode;
    uint64_t keys[MAX_LIST];
    int nkeys;
} ExpDef;

#define ALL_OPS ((1u << PH_INSERT) | (1u << PH_GET_HIT) | (1u << PH_GET_MISS) | (1u << PH_UPDATE))
#define M (1000000ull)

static const ExpDef experiments[] = {
    {1, "Flat array at scale (reference)",
     "How does the flat array (slot = key) behave from 1M to 100M keys? Every later result is compared with this.",
     {S_FLAT}, {0}, 1, {DAT_DENSE}, 1, {ORD_SEQ, ORD_RAND}, 2, ALL_OPS, VDC_PAGES_BASE,
     {1 * M, 10 * M, 25 * M, 50 * M, 75 * M, 100 * M}, 6},
    {2, "Cost of the row table (two-level vs flat)",
     "How much slower is the two-level layout than the flat array when its row table lives in L1 (2 MiB rows), "
     "L2 (64 KiB rows) or L3 (4 KiB rows)?",
     {S_FLAT, S_ROWS, S_ROWS, S_ROWS}, {0, 6, 10, 15}, 4, {DAT_DENSE}, 1, {ORD_SEQ, ORD_RAND}, 2, ALL_OPS,
     VDC_PAGES_BASE, {1 * M, 10 * M, 25 * M, 50 * M, 75 * M, 100 * M}, 6},
    {3, "Key density: what on-demand rows save",
     "How much memory does each layout need for dense, clustered and sparse keys, and what does it cost in speed?",
     {S_FLAT, S_ROWS, S_ROWS, S_ROWS}, {0, 6, 10, 15}, 4, {DAT_DENSE, DAT_CLUSTERED, DAT_SPARSE}, 3, {ORD_RAND}, 1,
     (1u << PH_INSERT) | (1u << PH_GET_HIT) | (1u << PH_GET_MISS), VDC_PAGES_BASE, {10 * M, 100 * M}, 2},
    {4, "Huge pages at scale",
     "Do 2 MiB pages still remove TLB misses when the data is several GB? (Base-page numbers: experiment 2.)",
     {S_FLAT, S_ROWS, S_ROWS}, {0, 10, 15}, 3, {DAT_DENSE}, 1, {ORD_SEQ, ORD_RAND}, 2, ALL_OPS, VDC_PAGES_HUGETLB,
     {10 * M, 50 * M, 100 * M}, 3},
    {5, "Baselines at scale",
     "Do the hash table and the skip list keep their relative speed at 10M-100M keys? (Flat / rows: experiments 1-2.)",
     {S_HASH, S_SKIPLIST}, {0, 0}, 2, {DAT_DENSE}, 1, {ORD_SEQ, ORD_RAND}, 2, ALL_OPS, VDC_PAGES_BASE,
     {1 * M, 10 * M, 25 * M, 50 * M, 75 * M, 100 * M}, 6},
};
#define NEXP ((int)(sizeof(experiments) / sizeof(experiments[0])))

typedef struct Options {
    int exp;
    uint64_t keys[MAX_LIST];
    int nkeys;
    uint64_t reps, max_ops, skiplist_max, run_len, seed;
    double budget_gib;
    int use_perf, plan_only, huge_needed, cpu;
    const char *csv;
} Options;

typedef struct SRes {
    int si, pattern, order, op; /* op = -1: skipped structure */
    uint64_t n, ops;
    double ns;
    double m[PC_COUNT];
    int valid[PC_COUNT];
    int ok, ran;
    uint64_t mem_plan, mem_used, mem_measured;
    char reason[128];
} SRes;

static Options opt;
static ExpDef E;
static ScaleStruct structs[MAX_STRUCTS];
static Perf perf;
static SRes *res;
static size_t nres, capres;
static double keygen_ns[DAT_PATTERN_COUNT][ORD_COUNT];
static int keygen_done[DAT_PATTERN_COUNT][ORD_COUNT];
static uint64_t budget_bytes;
static uint64_t peak_measured;
static long mem_available_kib;

/* ---------------------------------------------------------------- helpers */

static void add_res(const SRes *r)
{
    if (nres == capres) {
        capres = capres ? capres * 2 : 256;
        SRes *p = realloc(res, capres * sizeof(*res));
        if (p == NULL) {
            fprintf(stderr, "error: out of memory\n");
            exit(2);
        }
        res = p;
    }
    res[nres++] = *r;
}

static double gib(uint64_t b) { return (double)b / (double)SCALE_GIB; }

static void fmt_keys(char *b, size_t n, uint64_t k)
{
    if (k >= M && k % M == 0)
        snprintf(b, n, "%lluM", (unsigned long long)(k / M));
    else if (k >= 1000 && k % 1000 == 0)
        snprintf(b, n, "%lluK", (unsigned long long)(k / 1000));
    else
        snprintf(b, n, "%llu", (unsigned long long)k);
}

static void usage(FILE *f)
{
    fprintf(f, "Usage: dat_scale --experiment N [options]\n"
               "  --experiment N      1 flat at scale, 2 row-table cost, 3 key density, 4 huge pages, 5 baselines\n"
               "  --keys LIST         key counts (K = 1e3, M = 1e6), max 134217728 (default per experiment)\n"
               "  --reps N            repetitions, cycling through the counter groups (default 3)\n"
               "  --max-ops N         operations per get / update measurement (default 20M)\n"
               "  --budget-gib X      memory budget for program + structure (default 9, at most 10)\n"
               "  --skiplist-max N    largest skip list (default 10M; it is slow)\n"
               "  --run-len N         keys per run for the clustered pattern (default 1000)\n"
               "  --cpu N             pin to CPU N (Linux)\n"
               "  --no-perf           timings only\n"
               "  --csv FILE          append results as CSV\n"
               "  --seed N            (default 42)\n"
               "  --plan              print the memory plan and exit (allocates nothing)\n"
               "  --hugepages-needed  print the 2 MiB pages experiment 4 needs and exit\n");
}

static int parse_args(int argc, char **argv)
{
    memset(&opt, 0, sizeof(opt));
    opt.reps = 3;
    opt.max_ops = 20 * M;
    opt.skiplist_max = 10 * M;
    opt.run_len = 1000;
    opt.seed = 42;
    opt.budget_gib = 9.0;
    opt.use_perf = 1;
    opt.cpu = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            exit(0);
        }
        if (!strcmp(a, "--no-perf")) {
            opt.use_perf = 0;
            continue;
        }
        if (!strcmp(a, "--plan")) {
            opt.plan_only = 1;
            continue;
        }
        if (!strcmp(a, "--hugepages-needed")) {
            opt.huge_needed = 1;
            continue;
        }
        const char *v = (i + 1 < argc) ? argv[++i] : NULL;
        uint64_t x = 0;
        int n = 0;
        if (v == NULL) {
            fprintf(stderr, "error: missing value for %s\n", a);
            return -1;
        }
        if (!strcmp(a, "--keys")) {
            n = opt.nkeys = bench_parse_u64_list(v, opt.keys, MAX_LIST);
        } else if (!strcmp(a, "--csv")) {
            opt.csv = v;
            n = 1;
        } else if (!strcmp(a, "--budget-gib")) {
            char *end;
            opt.budget_gib = strtod(v, &end);
            n = (*end == '\0' && opt.budget_gib > 0) ? 1 : -1;
        } else {
            n = bench_parse_u64_list(v, &x, 1);
            if (!strcmp(a, "--experiment"))
                opt.exp = (int)x;
            else if (!strcmp(a, "--reps"))
                opt.reps = x;
            else if (!strcmp(a, "--max-ops"))
                opt.max_ops = x;
            else if (!strcmp(a, "--skiplist-max"))
                opt.skiplist_max = x;
            else if (!strcmp(a, "--run-len"))
                opt.run_len = x;
            else if (!strcmp(a, "--seed"))
                opt.seed = x;
            else if (!strcmp(a, "--cpu"))
                opt.cpu = (int)x;
            else {
                fprintf(stderr, "error: unknown option %s\n", a);
                return -1;
            }
        }
        if (n <= 0) {
            fprintf(stderr, "error: bad value for %s: %s\n", a, v);
            return -1;
        }
    }
    if (opt.exp < 1 || opt.exp > NEXP) {
        fprintf(stderr, "error: --experiment must be 1..%d\n", NEXP);
        return -1;
    }
    if (opt.budget_gib > SCALE_HARD_LIMIT_GIB) {
        fprintf(stderr, "error: --budget-gib may not exceed %.0f (hard limit)\n", SCALE_HARD_LIMIT_GIB);
        return -1;
    }
    if (opt.reps < 1 || opt.reps > MAX_REPS || opt.max_ops == 0 || opt.run_len == 0) {
        fprintf(stderr, "error: need 1 <= reps <= %d, max-ops > 0, run-len > 0\n", MAX_REPS);
        return -1;
    }
    for (int i = 0; i < opt.nkeys; i++)
        if (opt.keys[i] == 0 || opt.keys[i] > DAT_DOMAIN) {
            fprintf(stderr, "error: key counts must be 1 .. %llu\n", (unsigned long long)DAT_DOMAIN);
            return -1;
        }
    return 0;
}

static void build_structs(void)
{
    for (int i = 0; i < E.nstructs; i++) {
        ScaleStruct *s = &structs[i];
        memset(s, 0, sizeof(*s));
        s->kind = E.kinds[i];
        s->row_bits = E.row_bits[i];
        s->mode = E.mode;
        switch (s->kind) {
        case S_FLAT:
            snprintf(s->name, sizeof(s->name), "flat");
            break;
        case S_ROWS:
            snprintf(s->name, sizeof(s->name), "rows-%s", s->row_bits == 6 ? "4K" : s->row_bits == 10 ? "64K" : "2M");
            break;
        case S_HASH:
            snprintf(s->name, sizeof(s->name), "hash");
            break;
        case S_SKIPLIST:
            snprintf(s->name, sizeof(s->name), "skiplist");
            break;
        default:
            snprintf(s->name, sizeof(s->name), "?");
        }
    }
}

static unsigned page_row_bits(void)
{
    unsigned b = 0;
    while (((size_t)sizeof(DatNode) << b) < vdc_page_size())
        b++;
    return b;
}

/* Stop at once if the process holds more than the hard limit. */
static void guard_memory(void *t, const ScaleOps *ops, const char *where)
{
    const uint64_t now = scale_measured_bytes();
    if (now > peak_measured)
        peak_measured = now;
    if (now > (uint64_t)(SCALE_HARD_LIMIT_GIB * (double)SCALE_GIB)) {
        fprintf(stderr, "FATAL: process memory %.2f GiB after %s exceeds the %.0f GiB hard limit; stopping\n",
                gib(now), where, SCALE_HARD_LIMIT_GIB);
        ops->destroy(t);
        exit(3);
    }
    if (now > budget_bytes)
        fprintf(stderr, "warning: process memory %.2f GiB after %s is above the %.2f GiB budget\n", gib(now), where,
                gib(budget_bytes));
}

/* ---------------------------------------------------------------- measuring */

static double tns[PH_COUNT][MAX_REPS];
static double ctr[PH_COUNT][PC_COUNT][MAX_REPS];

static void run_structure(int si, const DatKeySet *ks, const ScalePlan *plan, const DatPerm *ord,
                          const DatPerm *ord_miss, uint64_t m, uint64_t m_miss)
{
    const ScaleStruct *s = &structs[si];
    const ScaleOps *ops = scale_ops(s->kind);
    char kb[32];
    fmt_keys(kb, sizeof(kb), ks->n);

    const uint64_t t_create = bench_now_ns();
    void *t = ops->create(s, ks->n, plan->rows_needed);
    if (t == NULL) {
        SRes r;
        memset(&r, 0, sizeof(r));
        r.si = si;
        r.pattern = ks->pattern;
        r.op = -1;
        r.n = ks->n;
        r.mem_plan = plan->total;
        snprintf(r.reason, sizeof(r.reason), "allocation failed (%s)", strerror(errno));
        add_res(&r);
        fprintf(stderr, "  %-9s %5s %-9s allocation failed: %s\n", s->name, kb, dat_pattern_name(ks->pattern),
                strerror(errno));
        return;
    }
    ops->prepare(t, ks);
    guard_memory(t, ops, "create");
    uint64_t measured = scale_measured_bytes();
    fprintf(stderr, "  %-9s %5s %-9s created + prepared in %.1f s, process memory %.2f GiB (plan %.2f)\n", s->name,
            kb, dat_pattern_name(ks->pattern), (double)(bench_now_ns() - t_create) / 1e9, gib(measured),
            gib(plan->total));

    const int skip_miss = (s->kind == S_FLAT && s->mode == VDC_PAGES_HUGETLB); /* keys beyond the mapping */

    for (int oi = 0; oi < E.norders; oi++) {
        const int order = E.orders[oi];
        int nt[PH_COUNT] = {0}, nc[PH_COUNT][PC_COUNT], ok[PH_COUNT], ran[PH_COUNT] = {0};
        uint64_t nops[PH_COUNT] = {0};
        memset(nc, 0, sizeof(nc));
        for (int op = 0; op < PH_COUNT; op++)
            ok[op] = 1;

        for (uint64_t rep = 0; rep < opt.reps; rep++) {
            const int set = perf.nsets > 0 ? (int)(rep % (uint64_t)perf.nsets) : -1;
            ops->clear(t, ks);
            for (int op = 0; op < PH_COUNT; op++) {
                if (op != PH_INSERT && !(E.ops_mask & (1u << op)))
                    continue;
                if (op == PH_GET_MISS && (skip_miss || m_miss == 0))
                    continue;
                PhaseIn in = {ks, order, ord, ord_miss, m, m_miss, &perf, set};
                PhaseOut out;
                ops->phase(t, op, &in, &out); /* times and counts only the structure work */
                const PerfReading rd = out.rd;
                if (out.ops == 0)
                    continue;
                tns[op][nt[op]++] = (double)out.ns / (double)out.ops;
                for (int ev = 0; ev < PC_COUNT; ev++)
                    if (rd.valid[ev])
                        ctr[op][ev][nc[op][ev]++] =
                            ev == PC_PAGE_FAULTS ? rd.value[ev] : rd.value[ev] / (double)out.ops;
                ok[op] &= out.ok;
                nops[op] = out.ops;
                ran[op] = 1;
                if (rep == 0 && op == PH_INSERT)
                    guard_memory(t, ops, "insert");
            }
        }

        const uint64_t used = ops->used_bytes(t) ? ops->used_bytes(t) : plan->bytes;
        measured = scale_measured_bytes();
        for (int op = 0; op < PH_COUNT; op++) {
            if (!ran[op] || (op != PH_INSERT && !(E.ops_mask & (1u << op))))
                continue;
            SRes r;
            memset(&r, 0, sizeof(r));
            r.si = si;
            r.pattern = ks->pattern;
            r.order = order;
            r.op = op;
            r.n = ks->n;
            r.ops = nops[op];
            r.ns = bench_median(tns[op], (size_t)nt[op]);
            for (int ev = 0; ev < PC_COUNT; ev++)
                if (nc[op][ev] > 0) {
                    r.m[ev] = bench_median(ctr[op][ev], (size_t)nc[op][ev]);
                    r.valid[ev] = 1;
                }
            r.ok = ok[op];
            r.ran = 1;
            r.mem_plan = plan->total;
            r.mem_used = used;
            r.mem_measured = measured;
            snprintf(r.reason, sizeof(r.reason), "%s", plan->reason);
            add_res(&r);
            fprintf(stderr, "    %-4s %-8s %8.1f ns/op%s\n", order_names[order], op_names[op], r.ns,
                    r.ok ? "" : "  WRONG RESULTS");
        }
    }
    ops->destroy(t);
}

static void run_keygen(const DatKeySet *ks, const DatPerm *ord)
{
    const ScaleOps *ops = scale_ops(S_KEYGEN);
    for (int oi = 0; oi < E.norders; oi++) {
        const int order = E.orders[oi];
        double s[MAX_REPS];
        for (uint64_t rep = 0; rep < opt.reps; rep++) {
            PhaseIn in = {ks, order, ord, ord, 0, 0, NULL, -1};
            PhaseOut out;
            ops->phase(NULL, PH_INSERT, &in, &out);
            s[rep] = (double)out.ns / (double)(out.ops ? out.ops : 1);
        }
        keygen_ns[ks->pattern][order] = bench_median(s, opt.reps);
        keygen_done[ks->pattern][order] = 1;
    }
}

/* ---------------------------------------------------------------- report */

static const SRes *find(int si, int pat, int order, int op, uint64_t n)
{
    for (size_t i = 0; i < nres; i++) {
        const SRes *r = &res[i];
        if (r->ran && r->si == si && r->pattern == pat && r->order == order && r->op == op && r->n == n)
            return r;
    }
    return NULL;
}

static const SRes *find_any(int si, int pat, uint64_t n)
{
    for (size_t i = 0; i < nres; i++)
        if (res[i].si == si && res[i].pattern == pat && res[i].n == n)
            return &res[i];
    return NULL;
}

#define M_NS (-1)
#define M_DTLB (-2)

static int metric(const SRes *r, int m, double *out)
{
    if (r == NULL)
        return 0;
    if (m == M_NS) {
        *out = r->ns;
        return 1;
    }
    if (m == M_DTLB) {
        if (!r->valid[PC_DTLB_LOAD_MISS] && !r->valid[PC_DTLB_STORE_MISS])
            return 0;
        *out = (r->valid[PC_DTLB_LOAD_MISS] ? r->m[PC_DTLB_LOAD_MISS] : 0) +
               (r->valid[PC_DTLB_STORE_MISS] ? r->m[PC_DTLB_STORE_MISS] : 0);
        return 1;
    }
    if (!r->valid[m])
        return 0;
    *out = r->m[m];
    return 1;
}

static const char *metric_title(int m)
{
    switch (m) {
    case M_NS: return "Time per operation (ns/op)";
    case M_DTLB: return "dTLB misses per operation (load + store)";
    case PC_INSTRUCTIONS: return "Instructions per operation";
    case PC_L1D_LOAD_MISS: return "L1D load misses per operation";
    case PC_LLC_MISS: return "LLC misses per operation";
    default: return "?";
    }
}

static void pivot(FILE *f, int pat, int order, int op, int m)
{
    double v;
    int any = 0;
    for (int ki = 0; ki < opt.nkeys && !any; ki++)
        for (int si = 0; si < E.nstructs && !any; si++)
            any = metric(find(si, pat, order, op, opt.keys[ki]), m, &v);
    if (!any)
        return;
    fprintf(f, "\n%s\n\n| keys |", metric_title(m));
    for (int si = 0; si < E.nstructs; si++)
        fprintf(f, " %s |", structs[si].name);
    fprintf(f, "\n|---|");
    for (int si = 0; si < E.nstructs; si++)
        fprintf(f, "---:|");
    fprintf(f, "\n");
    for (int ki = 0; ki < opt.nkeys; ki++) {
        char kb[32];
        fmt_keys(kb, sizeof(kb), opt.keys[ki]);
        fprintf(f, "| %s |", kb);
        for (int si = 0; si < E.nstructs; si++) {
            if (metric(find(si, pat, order, op, opt.keys[ki]), m, &v)) {
                if (m == M_NS || m == PC_INSTRUCTIONS)
                    fprintf(f, " %.1f |", v);
                else
                    fprintf(f, " %.3f |", v);
            } else {
                const SRes *any_r = find_any(si, pat, opt.keys[ki]);
                fprintf(f, " %s |", any_r != NULL && !any_r->ran ? "skipped" : "-");
            }
        }
        fprintf(f, "\n");
    }
}

static void print_report(void)
{
    FILE *f = stdout;
    fprintf(f, "# DAT scale experiment %d: %s\n\n%s\n\n## System\n\n", E.id, E.title, E.question);
    bench_print_system_info(f);

    fprintf(f, "\n## Configuration\n\n| Setting | Value |\n|---|---|\n| Structures |");
    for (int si = 0; si < E.nstructs; si++)
        fprintf(f, "%s %s", si ? "," : "", structs[si].name);
    fprintf(f, " |\n| Pages | %s |\n| Key counts |", page_names[E.mode]);
    for (int ki = 0; ki < opt.nkeys; ki++) {
        char kb[32];
        fmt_keys(kb, sizeof(kb), opt.keys[ki]);
        fprintf(f, "%s %s", ki ? "," : "", kb);
    }
    fprintf(f, " (key range 0 .. 2^27-1 = 134,217,728 keys) |\n| Patterns |");
    for (int pi = 0; pi < E.npatterns; pi++)
        fprintf(f, "%s %s", pi ? "," : "", dat_pattern_name((DatPattern)E.patterns[pi]));
    fprintf(f, " |\n| Orders |");
    for (int oi = 0; oi < E.norders; oi++)
        fprintf(f, "%s %s", oi ? "," : "", order_names[E.orders[oi]]);
    fprintf(f, " |\n| Operations |");
    for (int op = 0, first = 1; op < PH_COUNT; op++)
        if (E.ops_mask & (1u << op)) {
            fprintf(f, "%s %s", first ? "" : ",", op_names[op]);
            first = 0;
        }
    fprintf(f, " |\n| Repetitions | %llu (each measures one counter group; median time) |\n",
            (unsigned long long)opt.reps);
    fprintf(f, "| Operations per get / update measurement | min(keys, %llu) |\n", (unsigned long long)opt.max_ops);
    fprintf(f, "| Memory budget (program + structure) | %.2f GiB; hard stop at %.0f GiB |\n", gib(budget_bytes),
            SCALE_HARD_LIMIT_GIB);
    if (mem_available_kib > 0)
        fprintf(f, "| MemAvailable at start | %.2f GiB |\n", (double)mem_available_kib / (1024.0 * 1024.0));
    fprintf(f, "| Peak process memory measured | %.2f GiB |\n", gib(peak_measured));
    fprintf(f, "| Clustered run length | %llu keys |\n", (unsigned long long)opt.run_len);
    fprintf(f, "\nPatterns: **dense** = keys 0 .. N-1; **clustered** = runs of %llu consecutive keys at random places; "
               "**sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random "
               "order. Keys are computed on the fly (a reversible scramble), so no key lists use memory. They are "
               "computed in batches of %d outside the measured sections, so the times and counters below contain "
               "only the structure work.\n",
            (unsigned long long)opt.run_len, SCALE_CHUNK);

    /* Memory plan */
    fprintf(f, "\n## Memory plan\n\nComputed before anything was allocated. Memory = structure + %llu MiB for the "
               "program.\n\n| keys | pattern | structure | memory | decision |\n|---|---|---|---:|---|\n",
            (unsigned long long)(SCALE_OVERHEAD_BYTES / SCALE_MIB));
    for (int ki = 0; ki < opt.nkeys; ki++)
        for (int pi = 0; pi < E.npatterns; pi++)
            for (int si = 0; si < E.nstructs; si++) {
                const SRes *r = find_any(si, E.patterns[pi], opt.keys[ki]);
                if (r == NULL)
                    continue;
                char kb[32];
                fmt_keys(kb, sizeof(kb), opt.keys[ki]);
                fprintf(f, "| %s | %s | %s | %.2f GiB | %s %s |\n", kb, dat_pattern_name((DatPattern)E.patterns[pi]),
                        structs[si].name, gib(r->mem_plan), r->ran ? (opt.plan_only ? "✅ will run:" : "✅ ran:") : "⏭ skipped:", r->reason);
            }

    if (opt.plan_only)
        return;

    /* Checks */
    fprintf(f, "\n## Checks\n\n| Check | Target | Result | Worst | Where |\n|---|---|---|---|---|\n");
    {
        const SRes *bad = NULL;
        for (size_t i = 0; i < nres && bad == NULL; i++)
            if (res[i].ran && !res[i].ok)
                bad = &res[i];
        char w[160] = "-";
        if (bad != NULL)
            snprintf(w, sizeof(w), "%s / %s / %s / %llu keys", structs[bad->si].name, order_names[bad->order],
                     op_names[bad->op], (unsigned long long)bad->n);
        fprintf(f, "| Correctness (every get returned its own key) | all ok | %s | - | %s |\n",
                bad ? "❌ FAIL" : "✅ PASS", w);
    }
    for (int si = 0; si < E.nstructs; si++) {
        double worst = 0;
        char w[160] = "-";
        int found = 0;
        for (int pi = 0; pi < E.npatterns; pi++)
            for (int oi = 0; oi < E.norders; oi++)
                for (int op = 0; op < PH_COUNT; op++) {
                    double lo = 1e300, hi = 0, v;
                    int cnt = 0;
                    for (int ki = 0; ki < opt.nkeys; ki++)
                        if (metric(find(si, E.patterns[pi], E.orders[oi], op, opt.keys[ki]), PC_INSTRUCTIONS, &v) &&
                            v > 0) {
                            lo = v < lo ? v : lo;
                            hi = v > hi ? v : hi;
                            cnt++;
                        }
                    if (cnt >= 2 && hi / lo > worst) {
                        worst = hi / lo;
                        found = 1;
                        snprintf(w, sizeof(w), "%s / %s / %s", dat_pattern_name((DatPattern)E.patterns[pi]),
                                 order_names[E.orders[oi]], op_names[op]);
                    }
                }
        if (found)
            fprintf(f, "| O(1): instructions/op flat across key counts (%s) | max/min ≤ 1.10 | %s | %.2f | %s |\n",
                    structs[si].name, worst <= 1.10 ? "✅ PASS" : "❌ FAIL", worst, w);
        else
            fprintf(f, "| O(1): instructions/op flat across key counts (%s) | max/min ≤ 1.10 | n/a | needs counters and "
                       "≥ 2 key counts | - |\n",
                    structs[si].name);
    }
    {
        double worst = -1, v;
        for (size_t i = 0; i < nres; i++)
            if (res[i].ran && metric(&res[i], PC_PAGE_FAULTS, &v) && v > worst)
                worst = v;
        if (worst >= 0)
            fprintf(f, "| Page faults during measurements (median) | 0 | %s | %.0f | - |\n",
                    worst <= 0 ? "✅ PASS" : "❌ FAIL", worst);
    }
    fprintf(f, "| Peak process memory | ≤ %.2f GiB | %s | %.2f GiB | - |\n", gib(budget_bytes),
            peak_measured <= budget_bytes ? "✅ PASS" : "⚠️ above budget (below hard limit)", gib(peak_measured));

    /* Key generation */
    fprintf(f, "\n## Key generation cost (NOT included in the measurements)\n\nKeys are computed in batches of "
               "%d between measured sections; the timer and counters see only the structure work. For information, "
               "computing a key costs:\n\n| pattern |", SCALE_CHUNK);
    for (int oi = 0; oi < E.norders; oi++)
        fprintf(f, " %s ns/key |", order_names[E.orders[oi]]);
    fprintf(f, "\n|---|");
    for (int oi = 0; oi < E.norders; oi++)
        fprintf(f, "---:|");
    fprintf(f, "\n");
    for (int pi = 0; pi < E.npatterns; pi++) {
        fprintf(f, "| %s |", dat_pattern_name((DatPattern)E.patterns[pi]));
        for (int oi = 0; oi < E.norders; oi++) {
            if (keygen_done[E.patterns[pi]][E.orders[oi]])
                fprintf(f, " %.1f |", keygen_ns[E.patterns[pi]][E.orders[oi]]);
            else
                fprintf(f, " - |");
        }
        fprintf(f, "\n");
    }

    /* Memory per key */
    fprintf(f, "\n## Memory per key\n\nBytes of memory holding data, per key (ran) or needed (skipped). A 64 B node "
               "is the minimum.\n\n| keys | pattern |");
    for (int si = 0; si < E.nstructs; si++)
        fprintf(f, " %s |", structs[si].name);
    fprintf(f, "\n|---|---|");
    for (int si = 0; si < E.nstructs; si++)
        fprintf(f, "---:|");
    fprintf(f, "\n");
    for (int ki = 0; ki < opt.nkeys; ki++)
        for (int pi = 0; pi < E.npatterns; pi++) {
            char kb[32];
            fmt_keys(kb, sizeof(kb), opt.keys[ki]);
            fprintf(f, "| %s | %s |", kb, dat_pattern_name((DatPattern)E.patterns[pi]));
            for (int si = 0; si < E.nstructs; si++) {
                const SRes *r = find_any(si, E.patterns[pi], opt.keys[ki]);
                if (r == NULL)
                    fprintf(f, " - |");
                else if (r->ran)
                    fprintf(f, " %.0f |", (double)r->mem_used / (double)r->n);
                else if (r->mem_plan > SCALE_OVERHEAD_BYTES)
                    fprintf(f, " %.0f (skipped) |", (double)(r->mem_plan - SCALE_OVERHEAD_BYTES) / (double)r->n);
                else
                    fprintf(f, " skipped |");
            }
            fprintf(f, "\n");
        }

    /* Results */
    static const int metrics[] = {M_NS, PC_INSTRUCTIONS, PC_L1D_LOAD_MISS, PC_LLC_MISS, M_DTLB};
    fprintf(f, "\n## Results\n\nRows = key count, columns = structure. Medians per operation.\n");
    for (int pi = 0; pi < E.npatterns; pi++)
        for (int oi = 0; oi < E.norders; oi++)
            for (int op = 0; op < PH_COUNT; op++) {
                if (op != PH_INSERT && !(E.ops_mask & (1u << op)))
                    continue;
                fprintf(f, "\n### %s / %s / %s\n", dat_pattern_name((DatPattern)E.patterns[pi]),
                        order_names[E.orders[oi]], op_names[op]);
                for (size_t mi = 0; mi < sizeof(metrics) / sizeof(metrics[0]); mi++)
                    pivot(f, E.patterns[pi], E.orders[oi], op, metrics[mi]);
            }

    /* Extra cost vs flat */
    int flat_si = -1;
    for (int si = 0; si < E.nstructs; si++)
        if (structs[si].kind == S_FLAT)
            flat_si = si;
    if (flat_si >= 0 && E.nstructs > 1) {
        fprintf(f, "\n## Extra cost of each layout compared with the flat array\n\nCell = Δ ns/op (Δ LLC misses/op). "
                   "This is the price of the row table.\n");
        for (int pi = 0; pi < E.npatterns; pi++)
            for (int oi = 0; oi < E.norders; oi++)
                for (int op = 0; op < PH_COUNT; op++) {
                    if (op != PH_INSERT && !(E.ops_mask & (1u << op)))
                        continue;
                    fprintf(f, "\n**%s / %s / %s**\n\n| keys |", dat_pattern_name((DatPattern)E.patterns[pi]),
                            order_names[E.orders[oi]], op_names[op]);
                    for (int si = 0; si < E.nstructs; si++)
                        if (si != flat_si)
                            fprintf(f, " %s |", structs[si].name);
                    fprintf(f, "\n|---|");
                    for (int si = 0; si < E.nstructs; si++)
                        if (si != flat_si)
                            fprintf(f, "---:|");
                    fprintf(f, "\n");
                    for (int ki = 0; ki < opt.nkeys; ki++) {
                        char kb[32];
                        fmt_keys(kb, sizeof(kb), opt.keys[ki]);
                        fprintf(f, "| %s |", kb);
                        const SRes *fr = find(flat_si, E.patterns[pi], E.orders[oi], op, opt.keys[ki]);
                        for (int si = 0; si < E.nstructs; si++) {
                            if (si == flat_si)
                                continue;
                            const SRes *r = find(si, E.patterns[pi], E.orders[oi], op, opt.keys[ki]);
                            double a, b;
                            if (fr != NULL && r != NULL) {
                                if (metric(r, PC_LLC_MISS, &a) && metric(fr, PC_LLC_MISS, &b))
                                    fprintf(f, " %+.1f (%+.3f) |", r->ns - fr->ns, a - b);
                                else
                                    fprintf(f, " %+.1f |", r->ns - fr->ns);
                            } else {
                                fprintf(f, " - |");
                            }
                        }
                        fprintf(f, "\n");
                    }
                }
    }
}

static void write_csv(void)
{
    FILE *f = fopen(opt.csv, "a");
    if (f == NULL) {
        fprintf(stderr, "warning: cannot open %s: %s\n", opt.csv, strerror(errno));
        return;
    }
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0)
        fprintf(f, "experiment,structure,page_mode,pattern,order,keys,op,ops,ns_op,cycles_op,instr_op,l1d_load_miss_op,"
                   "llc_miss_op,dtlb_load_miss_op,dtlb_store_miss_op,page_faults,mem_plan_bytes,mem_used_bytes,"
                   "mem_measured_bytes,status,reason,ok\n");
    static const int cols[] = {PC_CYCLES, PC_INSTRUCTIONS, PC_L1D_LOAD_MISS, PC_LLC_MISS,
                               PC_DTLB_LOAD_MISS, PC_DTLB_STORE_MISS, PC_PAGE_FAULTS};
    for (size_t i = 0; i < nres; i++) {
        const SRes *r = &res[i];
        fprintf(f, "%d,%s,%s,%s,%s,%llu,%s,%llu,", E.id, structs[r->si].name, page_names[E.mode],
                dat_pattern_name((DatPattern)r->pattern), r->ran ? order_names[r->order] : "",
                (unsigned long long)r->n, r->ran ? op_names[r->op] : "", (unsigned long long)r->ops);
        if (r->ran)
            fprintf(f, "%.3f", r->ns);
        for (size_t c = 0; c < sizeof(cols) / sizeof(cols[0]); c++) {
            if (r->ran && r->valid[cols[c]])
                fprintf(f, ",%.4f", r->m[cols[c]]);
            else
                fprintf(f, ",");
        }
        char reason[128];
        snprintf(reason, sizeof(reason), "%s", r->reason);
        for (char *p = reason; *p; p++)
            if (*p == ',')
                *p = ';';
        fprintf(f, ",%llu,%llu,%llu,%s,%s,%d\n", (unsigned long long)r->mem_plan, (unsigned long long)r->mem_used,
                (unsigned long long)r->mem_measured, r->ran ? "ran" : "skipped", reason, r->ok);
    }
    for (int p = 0; p < DAT_PATTERN_COUNT; p++)
        for (int o = 0; o < ORD_COUNT; o++)
            if (keygen_done[p][o])
                fprintf(f, "%d,keygen,%s,%s,%s,,insert,,%.3f,,,,,,,,0,0,0,ran,key generation only,1\n", E.id,
                        page_names[E.mode], dat_pattern_name((DatPattern)p), order_names[o], keygen_ns[p][o]);
    fclose(f);
}

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    if (parse_args(argc, argv) != 0) {
        usage(stderr);
        return 2;
    }
    E = experiments[opt.exp - 1];
    if (opt.nkeys == 0) {
        memcpy(opt.keys, E.keys, sizeof(uint64_t) * (size_t)E.nkeys);
        opt.nkeys = E.nkeys;
    }
    build_structs();
    if (opt.cpu >= 0)
        (void)vdc_pin_thread(opt.cpu);

    /* Budget: configured value; for base pages also never more than what is free now minus 0.5 GiB. */
    budget_bytes = (uint64_t)(opt.budget_gib * (double)SCALE_GIB);
    mem_available_kib = bench_meminfo_value("MemAvailable");
    if (E.mode == VDC_PAGES_BASE && mem_available_kib > 0) {
        const uint64_t avail = (uint64_t)mem_available_kib * 1024u;
        const uint64_t margin = SCALE_GIB / 2;
        if (avail > margin && avail - margin < budget_bytes) {
            fprintf(stderr, "note: only %.2f GiB available; budget lowered from %.2f to %.2f GiB\n", gib(avail),
                    gib(budget_bytes), gib(avail - margin));
            budget_bytes = avail - margin;
        }
    }
    const long huge_free = bench_meminfo_value("HugePages_Free");

    if (E.mode == VDC_PAGES_HUGETLB && !opt.plan_only && !opt.huge_needed) {
        void *p = vdc_map(VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB, 1);
        if (p == NULL) {
            fprintf(stderr, "ERROR: no 2 MiB hugetlb pages available (%s). Reserve them first "
                            "(scripts/scale/run_scale.sh does it in its huge-page phase).\n",
                    strerror(errno));
            return 2;
        }
        vdc_unmap(p, VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB);
    }

    if (opt.use_perf && !opt.plan_only && !opt.huge_needed) {
        perf_init(&perf, 1);
        if (perf.nsets > (int)opt.reps)
            fprintf(stderr, "warning: %llu repetitions but %d counter groups: use --reps %d to measure every group\n",
                    (unsigned long long)opt.reps, perf.nsets, perf.nsets);
    }

    const unsigned bits[4] = {page_row_bits(), 6, 10, 15};
    uint64_t huge_max = 0;
    uint64_t keygen_n = 0;
    for (int ki = 0; ki < opt.nkeys; ki++)
        if (opt.keys[ki] <= 10 * M && opt.keys[ki] > keygen_n)
            keygen_n = opt.keys[ki];

    fprintf(stderr, "== experiment %d: %s (budget %.2f GiB)\n", E.id, E.title, gib(budget_bytes));
    for (int ki = 0; ki < opt.nkeys; ki++) {
        const uint64_t n = opt.keys[ki];
        for (int pi = 0; pi < E.npatterns; pi++) {
            const int pat = E.patterns[pi];
            DatKeySet ks;
            char kb[32];
            fmt_keys(kb, sizeof(kb), n);
            if (dat_keys_init(&ks, (DatPattern)pat, n, DAT_DOMAIN, opt.run_len, opt.seed) != 0) {
                fprintf(stderr, "  %s %s: key set does not fit the key range, skipped\n", kb,
                        dat_pattern_name((DatPattern)pat));
                continue;
            }
            uint64_t used[4];
            const uint64_t tp = bench_now_ns();
            plan_count_rows(&ks, bits, 4, used);
            fprintf(stderr, "  %s %s keys: planned in %.1f s\n", kb, dat_pattern_name((DatPattern)pat),
                    (double)(bench_now_ns() - tp) / 1e9);

            DatPerm ord, ord_miss;
            dat_perm_init(&ord, n, opt.seed ^ 0xA5A5u);
            dat_perm_init(&ord_miss, ks.n_miss > 0 ? ks.n_miss : 1, opt.seed ^ 0x5A5Au);
            const uint64_t m = n < opt.max_ops ? n : opt.max_ops;
            const uint64_t m_miss = ks.n_miss < opt.max_ops ? ks.n_miss : opt.max_ops;

            for (int si = 0; si < E.nstructs; si++) {
                const ScalePlan plan = plan_structure(&structs[si], &ks, used, budget_bytes,
                                                      opt.huge_needed ? -1 : huge_free, opt.skiplist_max);
                if (plan.run && plan.huge_pages > huge_max)
                    huge_max = plan.huge_pages;
                if (!plan.run || opt.plan_only || opt.huge_needed) {
                    SRes r;
                    memset(&r, 0, sizeof(r));
                    r.si = si;
                    r.pattern = pat;
                    r.op = -1;
                    r.n = n;
                    r.ran = 0;
                    r.mem_plan = plan.total ? plan.total : plan.bytes + SCALE_OVERHEAD_BYTES;
                    snprintf(r.reason, sizeof(r.reason), "%s", plan.reason);
                    if (plan.run && opt.plan_only)
                        r.ran = 1; /* shown as "would run" in the plan */
                    add_res(&r);
                    if (!plan.run)
                        fprintf(stderr, "  %-9s %5s %-9s skipped: %s\n", structs[si].name, kb,
                                dat_pattern_name((DatPattern)pat), plan.reason);
                    continue;
                }
                run_structure(si, &ks, &plan, &ord, &ord_miss, m, m_miss);
            }
            if (n == keygen_n && !opt.plan_only && !opt.huge_needed)
                run_keygen(&ks, &ord);
        }
    }

    if (opt.huge_needed) {
        printf("%llu\n", (unsigned long long)huge_max);
        return 0;
    }

    print_report();
    if (opt.csv != NULL && !opt.plan_only)
        write_csv();
    perf_close(&perf);

    int failed = 0;
    for (size_t i = 0; i < nres; i++)
        failed |= (res[i].ran && res[i].op >= 0 && !res[i].ok);
    if (failed)
        fprintf(stderr, "ERROR: wrong results detected (see the Checks table)\n");
    free(res);
    return failed ? 1 : 0;
}
