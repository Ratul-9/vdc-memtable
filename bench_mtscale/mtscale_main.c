/*
 * dat_mtscale - experiment 6: threads at every key count (1M - 100M keys).
 *
 * For each key count N, thread count T, layout (flat, rows-2M, ...) and lock
 * design (mutex, spin, nodelock, seqlock, ...):
 *   load : T threads insert keys 0 .. N-1 (random order, shared out round-robin)
 *          into an empty table (timed, repeated --reps times)
 *   A / B / C / W : T threads run 50/50, 95/5, 100% get, 100% update on the full
 *          table with uniform or zipf (theta 0.99, hottest keys 0, 1, 2, ...) keys
 * Every value is four equal 64-bit words holding the key, so torn or misrouted
 * reads are detected on every get.
 *
 * Memory rules (same as dat_scale): one structure exists at a time; its memory,
 * the load keys (N x 4 B) and the per-thread operation lists are computed before
 * anything is allocated, and the run is skipped if that is over the budget
 * (default 9 GiB, at most 10). Real memory is measured after creation; above
 * 10 GiB the program stops.
 */
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_common.h"
#include "dat/dat_conc.h"
#include "dat/dat_keys.h"

#define MAX_LIST 16
#define MAX_THREADS 256
#define M (1000000ull)
#define GIB ((uint64_t)1 << 30)
#define MIB ((uint64_t)1 << 20)
#define OVERHEAD (256 * MIB)
#define HARD_LIMIT ((uint64_t)10 * GIB)

enum { MIX_LOAD, MIX_A, MIX_B, MIX_C, MIX_W, MIX_COUNT };
static const char *const mix_names[MIX_COUNT] = {"load", "A", "B", "C", "W"};
static const char *const mix_desc[MIX_COUNT] = {"insert every key into an empty table", "50% get / 50% update",
                                                "95% get / 5% update", "100% get", "100% update"};
static const int read_pct[MIX_COUNT] = {0, 50, 95, 100, 0};
enum { D_UNIFORM, D_ZIPF, D_INTERLEAVED, D_COUNT };
static const char *const dist_names[D_COUNT] = {"uniform", "zipf", "interleaved"};
static const char *const layout_names[] = {"flat", "rows-64K", "rows-2M"};
static const unsigned layout_bits[] = {0, 10, 15};
#define NLAYOUTS 3
static const char *const design_names[DC_MODE_COUNT] = {"mutex", "spin", "rwlock", "nodelock", "seqlock"};
static const char *const page_names[2] = {"base", "hugetlb"};

typedef struct Options {
    int layouts[NLAYOUTS];
    int nlayouts;
    int designs[DC_MODE_COUNT];
    int ndesigns;
    uint64_t keys[MAX_LIST];
    int nkeys;
    uint64_t threads[MAX_LIST];
    int nthreads;
    int mixes[MIX_COUNT];
    int nmixes;
    int dists[D_COUNT];
    int ndists;
    uint64_t ops, reps, sample, seed;
    double budget_gib;
    int page_mode;
    uint64_t cpus[MAX_THREADS];
    int ncpus;
    const char *csv;
    int plan_only;
} Options;

typedef struct Result {
    int layout, design, dist, mix; /* dist = -1 for load */
    uint64_t n, threads;
    double mops, p50, p99, p999;
    uint64_t bad_value, bad_status;
    int count_ok; /* load: every key present afterwards; -1 = not checked */
    int ran;
    uint64_t mem_plan, mem_measured;
    char reason[128];
} Result;

static Options opt;
static Result *res;
static size_t nres, capres;
static uint64_t budget;
static uint64_t peak_measured;

/* ---------------------------------------------------------------- helpers */

static void add_res(const Result *r)
{
    if (nres == capres) {
        capres = capres ? capres * 2 : 512;
        Result *p = realloc(res, capres * sizeof(*res));
        if (p == NULL) {
            fprintf(stderr, "error: out of memory\n");
            exit(2);
        }
        res = p;
    }
    res[nres++] = *r;
}

static double gib(uint64_t b) { return (double)b / (double)GIB; }

static void fmt_keys(char *b, size_t n, uint64_t k)
{
    if (k >= M && k % M == 0)
        snprintf(b, n, "%lluM", (unsigned long long)(k / M));
    else if (k >= 1000 && k % 1000 == 0)
        snprintf(b, n, "%lluK", (unsigned long long)(k / 1000));
    else
        snprintf(b, n, "%llu", (unsigned long long)k);
}

static uint64_t measured_bytes(void)
{
    const long rss = bench_proc_status_kib("VmRSS");
    const long huge = bench_proc_status_kib("HugetlbPages");
    if (rss < 0)
        return 0;
    return ((uint64_t)rss + (uint64_t)(huge > 0 ? huge : 0)) * 1024u;
}

static int cpu_of(int idx)
{
    if (opt.ncpus > 0)
        return idx < opt.ncpus ? (int)opt.cpus[idx] : -1;
    return idx;
}

static uint64_t max_threads(void)
{
    uint64_t m = 0;
    for (int i = 0; i < opt.nthreads; i++)
        m = opt.threads[i] > m ? opt.threads[i] : m;
    return m;
}

static void usage(FILE *f)
{
    fprintf(f, "Usage: dat_mtscale [options]   (experiment 6: threads at every key count)\n"
               "  --layouts LIST   flat,rows-64K,rows-2M                 (default flat,rows-2M)\n"
               "  --designs LIST   mutex,spin,rwlock,nodelock,seqlock      (default mutex,spin,nodelock,seqlock)\n"
               "  --keys LIST      key counts, max 134217728               (default 1M,10M,25M,50M,75M,100M)\n"
               "  --threads LIST                                           (default 1,2,4,6,8,12)\n"
               "  --mixes LIST     load,A,B,C,W                            (default load,A,C,W)\n"
               "  --dists LIST     uniform,zipf,interleaved                (default uniform,zipf)\n"
               "  --ops N          operations per thread per workload      (default 500K)\n"
               "  --reps N         repetitions, median throughput          (default 3)\n"
               "  --sample N       time 1 of every N operations            (default 64)\n"
               "  --pages base|hugetlb                                     (default hugetlb)\n"
               "  --cpus LIST      CPU of thread 0, 1, ...                 (default 0,1,2,...)\n"
               "  --budget-gib X   memory budget (default 9, at most 10)\n"
               "  --csv FILE       append results as CSV\n"
               "  --seed N         (default 42)\n"
               "  --plan           print the memory plan and exit\n");
}

static int parse_args(int argc, char **argv)
{
    memset(&opt, 0, sizeof(opt));
    opt.layouts[0] = 0;
    opt.layouts[1] = 2;
    opt.nlayouts = 2;
    const int dd[] = {DC_MUTEX, DC_SPIN, DC_NODELOCK, DC_SEQLOCK};
    memcpy(opt.designs, dd, sizeof(dd));
    opt.ndesigns = 4;
    const uint64_t kk[] = {1 * M, 10 * M, 25 * M, 50 * M, 75 * M, 100 * M};
    memcpy(opt.keys, kk, sizeof(kk));
    opt.nkeys = 6;
    const uint64_t tt[] = {1, 2, 4, 6, 8, 12};
    memcpy(opt.threads, tt, sizeof(tt));
    opt.nthreads = 6;
    const int mm[] = {MIX_LOAD, MIX_A, MIX_C, MIX_W};
    memcpy(opt.mixes, mm, sizeof(mm));
    opt.nmixes = 4;
    opt.dists[0] = D_UNIFORM;
    opt.dists[1] = D_ZIPF;
    opt.ndists = 2;
    opt.ops = 500000;
    opt.reps = 3;
    opt.sample = 64;
    opt.seed = 42;
    opt.budget_gib = 9.0;
    opt.page_mode = VDC_PAGES_HUGETLB;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            exit(0);
        }
        if (!strcmp(a, "--plan")) {
            opt.plan_only = 1;
            continue;
        }
        const char *v = (i + 1 < argc) ? argv[++i] : NULL;
        int n = 0;
        if (v == NULL) {
            fprintf(stderr, "error: missing value for %s\n", a);
            return -1;
        }
        if (!strcmp(a, "--layouts"))
            n = opt.nlayouts = bench_parse_name_list(v, layout_names, NLAYOUTS, opt.layouts, NLAYOUTS);
        else if (!strcmp(a, "--designs"))
            n = opt.ndesigns = bench_parse_name_list(v, design_names, DC_MODE_COUNT, opt.designs, DC_MODE_COUNT);
        else if (!strcmp(a, "--keys"))
            n = opt.nkeys = bench_parse_u64_list(v, opt.keys, MAX_LIST);
        else if (!strcmp(a, "--threads"))
            n = opt.nthreads = bench_parse_u64_list(v, opt.threads, MAX_LIST);
        else if (!strcmp(a, "--mixes"))
            n = opt.nmixes = bench_parse_name_list(v, mix_names, MIX_COUNT, opt.mixes, MIX_COUNT);
        else if (!strcmp(a, "--dists"))
            n = opt.ndists = bench_parse_name_list(v, dist_names, D_COUNT, opt.dists, D_COUNT);
        else if (!strcmp(a, "--ops"))
            n = bench_parse_u64_list(v, &opt.ops, 1);
        else if (!strcmp(a, "--reps"))
            n = bench_parse_u64_list(v, &opt.reps, 1);
        else if (!strcmp(a, "--sample"))
            n = bench_parse_u64_list(v, &opt.sample, 1);
        else if (!strcmp(a, "--seed"))
            n = bench_parse_u64_list(v, &opt.seed, 1);
        else if (!strcmp(a, "--cpus"))
            n = opt.ncpus = bench_parse_u64_list(v, opt.cpus, MAX_THREADS);
        else if (!strcmp(a, "--pages")) {
            int pm;
            n = bench_parse_name_list(v, page_names, 2, &pm, 1);
            opt.page_mode = pm;
        } else if (!strcmp(a, "--budget-gib")) {
            char *end;
            opt.budget_gib = strtod(v, &end);
            n = (*end == '\0' && opt.budget_gib > 0) ? 1 : -1;
        } else if (!strcmp(a, "--csv")) {
            opt.csv = v;
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
    if (opt.budget_gib > 10.0) {
        fprintf(stderr, "error: --budget-gib may not exceed 10\n");
        return -1;
    }
    if (opt.ops == 0 || opt.reps == 0 || opt.reps > 20 || opt.sample == 0) {
        fprintf(stderr, "error: need ops > 0, 1 <= reps <= 20, sample > 0\n");
        return -1;
    }
    for (int i = 0; i < opt.nkeys; i++)
        if (opt.keys[i] == 0 || opt.keys[i] > DAT_DOMAIN) {
            fprintf(stderr, "error: key counts must be 1 .. %llu\n", (unsigned long long)DAT_DOMAIN);
            return -1;
        }
    for (int i = 0; i < opt.nthreads; i++)
        if (opt.threads[i] == 0 || opt.threads[i] > MAX_THREADS) {
            fprintf(stderr, "error: threads must be 1 .. %d\n", MAX_THREADS);
            return -1;
        }
    return 0;
}

/* ---------------------------------------------------------------- memory plan */

static uint64_t round_up(uint64_t x, uint64_t u) { return (x + u - 1) / u * u; }

/* Bytes of the structure for dense keys 0 .. n-1. */
static uint64_t structure_bytes(int layout, uint64_t n)
{
    const uint64_t unit = opt.page_mode == VDC_PAGES_HUGETLB ? VDC_HUGE_PAGE_SIZE : vdc_page_size();
    uint64_t b = (uint64_t)DC_STRIPES * 64;
    if (layout_bits[layout] == 0) {
        const uint64_t per = VDC_HUGE_PAGE_SIZE / 64;
        b += round_up(round_up(n, per) * 64, unit);
    } else {
        const unsigned r = layout_bits[layout];
        const uint64_t rows = (n + ((uint64_t)1 << r) - 1) >> r;
        b += round_up((DAT_DOMAIN >> r) * 4, unit) + round_up(rows * (64ull << r), unit);
    }
    return b;
}

static uint64_t huge_pages_of(int layout, uint64_t n)
{
    if (opt.page_mode != VDC_PAGES_HUGETLB)
        return 0;
    return (structure_bytes(layout, n) - (uint64_t)DC_STRIPES * 64) / VDC_HUGE_PAGE_SIZE;
}

/* Structure + load keys + operation lists of the largest thread count + program. */
static uint64_t plan_total(int layout, uint64_t n)
{
    const int combos = (opt.nmixes - (opt.mixes[0] == MIX_LOAD ? 1 : 0)) * opt.ndists;
    return structure_bytes(layout, n) + n * 4 + max_threads() * opt.ops * 5 * (uint64_t)(combos > 0 ? combos : 0) +
           OVERHEAD;
}

/* ---------------------------------------------------------------- workloads */

typedef struct Zipf {
    double n, theta, alpha, zetan, eta;
} Zipf;

static void zipf_init(Zipf *z, uint64_t n, double theta)
{
    double zetan = 0.0;
    for (uint64_t i = 1; i <= n; i++)
        zetan += 1.0 / pow((double)i, theta);
    z->n = (double)n;
    z->theta = theta;
    z->alpha = 1.0 / (1.0 - theta);
    z->zetan = zetan;
    z->eta = (1.0 - pow(2.0 / (double)n, 1.0 - theta)) / (1.0 - (1.0 + pow(0.5, theta)) / zetan);
}

static uint32_t zipf_next(const Zipf *z, BenchRng *r)
{
    const double u = (double)(bench_rng_next(r) >> 11) / 9007199254740992.0;
    const double uz = u * z->zetan;
    if (uz < 1.0)
        return 0;
    if (uz < 1.0 + pow(0.5, z->theta))
        return 1;
    uint64_t k = (uint64_t)(z->n * pow(z->eta * u - z->eta + 1.0, z->alpha));
    return (uint32_t)(k >= (uint64_t)z->n ? (uint64_t)z->n - 1 : k);
}

typedef struct Work {
    uint32_t *keys;
    uint8_t *get;
    size_t n;
} Work;

/* Operation lists for every (dist, mix) and thread, for one N and T. */
static Work *work_lists; /* [combo * T + t] */
static int ncombos;
static int combo_dist[16], combo_mix[16];

static void free_work(uint64_t T)
{
    if (work_lists == NULL)
        return;
    for (uint64_t i = 0; i < (uint64_t)ncombos * T; i++) {
        free(work_lists[i].keys);
        free(work_lists[i].get);
    }
    free(work_lists);
    work_lists = NULL;
}

static void make_work(uint64_t n, uint64_t T, const Zipf *z)
{
    ncombos = 0;
    for (int mi = 0; mi < opt.nmixes; mi++) {
        if (opt.mixes[mi] == MIX_LOAD)
            continue;
        for (int di = 0; di < opt.ndists; di++) {
            combo_mix[ncombos] = opt.mixes[mi];
            combo_dist[ncombos] = opt.dists[di];
            ncombos++;
        }
    }
    work_lists = calloc((size_t)ncombos * T + 1, sizeof(Work));
    for (int c = 0; c < ncombos; c++)
        for (uint64_t t = 0; t < T; t++) {
            Work *w = &work_lists[(uint64_t)c * T + t];
            w->n = opt.ops;
            w->keys = malloc(w->n * sizeof(uint32_t));
            w->get = malloc(w->n);
            if (w->keys == NULL || w->get == NULL) {
                fprintf(stderr, "error: out of memory for operation lists\n");
                exit(2);
            }
            BenchRng r = {opt.seed * 1000003ull + n + (uint64_t)c * 7919u + t * 104729u};
            for (size_t i = 0; i < w->n; i++) {
                uint32_t key;
                if (combo_dist[c] == D_UNIFORM)
                    key = (uint32_t)bench_rng_below(&r, n);
                else if (combo_dist[c] == D_ZIPF)
                    key = zipf_next(z, &r);
                else
                    key = (uint32_t)(((uint64_t)i * T + t) % n);
                w->keys[i] = key;
                w->get[i] = bench_rng_below(&r, 100) < (uint64_t)read_pct[combo_mix[c]];
            }
        }
}

/* ---------------------------------------------------------------- threads */

typedef struct Ctx {
    DatConc *c;
    int cpu;
    int load;           /* 1 = load: insert load_keys[id], load_keys[id + T], ... */
    const uint32_t *load_keys;
    uint64_t n, T, id;
    const Work *work;
    uint64_t seq_base;
    uint64_t t_end, ops, bad_value, bad_status;
    double *lat;
    size_t nlat;
} Ctx;

static int g_ready, g_go;

static inline void make_value(uint64_t w[4], uint32_t key, uint64_t seq)
{
    const uint64_t x = ((uint64_t)key << 32) | (seq & 0xffffffffu);
    w[0] = w[1] = w[2] = w[3] = x;
}

static void *worker(void *arg)
{
    Ctx *x = (Ctx *)arg;
    if (x->cpu >= 0)
        (void)vdc_pin_thread(x->cpu);
    __atomic_fetch_add(&g_ready, 1, __ATOMIC_ACQ_REL);
    while (!__atomic_load_n(&g_go, __ATOMIC_ACQUIRE))
        vdc_cpu_relax();

    uint64_t w[4];
    uint64_t seq = x->seq_base;
    if (x->load) {
        uint64_t i = 0;
        for (uint64_t idx = x->id; idx < x->n; idx += x->T, i++) {
            const uint32_t key = x->load_keys[idx];
            const int sample = (i % opt.sample) == 0;
            const uint64_t t0 = sample ? bench_now_ns() : 0;
            make_value(w, key, ++seq);
            if (dc_put(x->c, key, w, 32, seq) != MT_OK)
                x->bad_status++;
            if (sample)
                x->lat[x->nlat++] = (double)(bench_now_ns() - t0);
        }
        x->ops = i;
    } else {
        const Work *wk = x->work;
        for (size_t i = 0; i < wk->n; i++) {
            const uint32_t key = wk->keys[i];
            const int sample = (i % opt.sample) == 0;
            const uint64_t t0 = sample ? bench_now_ns() : 0;
            if (wk->get[i]) {
                uint32_t len = 0;
                const int rc = dc_get(x->c, key, w, &len, NULL);
                if (rc != MT_OK)
                    x->bad_status++;
                else if (len != 32 || w[0] != w[1] || w[1] != w[2] || w[2] != w[3] || (uint32_t)(w[0] >> 32) != key)
                    x->bad_value++;
            } else {
                make_value(w, key, ++seq);
                if (dc_put(x->c, key, w, 32, seq) != MT_OK)
                    x->bad_status++;
            }
            if (sample)
                x->lat[x->nlat++] = (double)(bench_now_ns() - t0);
        }
        x->ops = wk->n;
    }
    x->t_end = bench_now_ns();
    return NULL;
}

static int cmp_d(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double pct(const double *v, size_t n, double p) { return n ? v[(size_t)(p * (double)(n - 1) + 0.5)] : 0.0; }

/* One measured cell: load (combo < 0) or a workload combo. */
static void run_cell(DatConc *c, int layout, int design, uint64_t n, uint64_t T, int combo, const uint32_t *load_keys,
                     uint64_t mem_plan)
{
    Ctx *ctx = calloc(T, sizeof(Ctx));
    pthread_t *th = calloc(T, sizeof(pthread_t));
    const size_t per_thread_ops = combo < 0 ? (size_t)((n + T - 1) / T) : (size_t)opt.ops;
    const size_t lat_per = per_thread_ops / opt.sample + 2;
    double *lat = malloc(lat_per * T * opt.reps * sizeof(double));
    double mops[20];
    size_t nlat = 0;
    uint64_t bad_value = 0, bad_status = 0;
    int count_ok = -1;
    if (ctx == NULL || th == NULL || lat == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(2);
    }

    for (uint64_t rep = 0; rep < opt.reps; rep++) {
        if (combo < 0)
            dc_clear(c);
        g_ready = 0;
        g_go = 0;
        for (uint64_t t = 0; t < T; t++) {
            Ctx *x = &ctx[t];
            memset(x, 0, sizeof(*x));
            x->c = c;
            x->cpu = cpu_of((int)t);
            x->load = combo < 0;
            x->load_keys = load_keys;
            x->n = n;
            x->T = T;
            x->id = t;
            x->work = combo < 0 ? NULL : &work_lists[(uint64_t)combo * T + t];
            x->seq_base = ((rep + 1) << 40) + (t << 32);
            x->lat = lat + nlat + t * lat_per;
            if (pthread_create(&th[t], NULL, worker, x) != 0) {
                fprintf(stderr, "error: cannot create thread\n");
                exit(2);
            }
        }
        while (__atomic_load_n(&g_ready, __ATOMIC_ACQUIRE) < (int)T)
            vdc_cpu_relax();
        const uint64_t t0 = bench_now_ns();
        __atomic_store_n(&g_go, 1, __ATOMIC_RELEASE);
        uint64_t t1 = t0, ops = 0;
        for (uint64_t t = 0; t < T; t++) {
            pthread_join(th[t], NULL);
            t1 = ctx[t].t_end > t1 ? ctx[t].t_end : t1;
            ops += ctx[t].ops;
            bad_value += ctx[t].bad_value;
            bad_status += ctx[t].bad_status;
        }
        mops[rep] = t1 > t0 ? (double)ops / ((double)(t1 - t0) / 1e3) : 0.0;
        /* compact this rep's samples */
        size_t pos = nlat;
        for (uint64_t t = 0; t < T; t++) {
            memmove(lat + pos, ctx[t].lat, ctx[t].nlat * sizeof(double));
            pos += ctx[t].nlat;
        }
        nlat = pos;
        if (combo < 0 && rep == 0) {
            const uint64_t now = measured_bytes();
            peak_measured = now > peak_measured ? now : peak_measured;
            if (now > HARD_LIMIT) {
                fprintf(stderr, "FATAL: process memory %.2f GiB exceeds the 10 GiB hard limit; stopping\n", gib(now));
                exit(3);
            }
            count_ok = dc_count(c) == n;
        }
    }

    qsort(lat, nlat, sizeof(double), cmp_d);
    Result r;
    memset(&r, 0, sizeof(r));
    r.layout = layout;
    r.design = design;
    r.dist = combo < 0 ? -1 : combo_dist[combo];
    r.mix = combo < 0 ? MIX_LOAD : combo_mix[combo];
    r.n = n;
    r.threads = T;
    r.mops = bench_median(mops, opt.reps);
    r.p50 = pct(lat, nlat, 0.50);
    r.p99 = pct(lat, nlat, 0.99);
    r.p999 = pct(lat, nlat, 0.999);
    r.bad_value = bad_value;
    r.bad_status = bad_status;
    r.count_ok = count_ok;
    r.ran = 1;
    r.mem_plan = mem_plan;
    r.mem_measured = measured_bytes();
    add_res(&r);

    char kb[32];
    fmt_keys(kb, sizeof(kb), n);
    fprintf(stderr, "    %-8s %-8s %5s T=%-3llu %-8s %-4s %8.1f Mops/s  p99 %6.0f ns%s\n", layout_names[layout],
            design_names[design], kb, (unsigned long long)T, combo < 0 ? "-" : dist_names[combo_dist[combo]],
            mix_names[r.mix], r.mops, r.p99, (bad_value || bad_status || count_ok == 0) ? "  WRONG RESULTS" : "");
    free(ctx);
    free(th);
    free(lat);
}

/* ---------------------------------------------------------------- report */

static const Result *find(int layout, int design, uint64_t n, uint64_t T, int dist, int mix)
{
    for (size_t i = 0; i < nres; i++) {
        const Result *r = &res[i];
        if (r->ran && r->layout == layout && r->design == design && r->n == n && r->threads == T && r->mix == mix &&
            (mix == MIX_LOAD || r->dist == dist))
            return r;
    }
    return NULL;
}

/* Workloads in report order: load, then mix x dist. */
static int nwl;
static int wl_mix[16], wl_dist[16];

static void build_workloads(void)
{
    nwl = 0;
    for (int mi = 0; mi < opt.nmixes; mi++) {
        if (opt.mixes[mi] == MIX_LOAD) {
            wl_mix[nwl] = MIX_LOAD;
            wl_dist[nwl++] = -1;
            continue;
        }
        for (int di = 0; di < opt.ndists; di++) {
            wl_mix[nwl] = opt.mixes[mi];
            wl_dist[nwl++] = opt.dists[di];
        }
    }
}

static void wl_label(char *b, size_t n, int w)
{
    if (wl_mix[w] == MIX_LOAD)
        snprintf(b, n, "load");
    else
        snprintf(b, n, "%s / %s", mix_names[wl_mix[w]], dist_names[wl_dist[w]]);
}

static void print_report(void)
{
    FILE *f = stdout;
    const uint64_t tmax = max_threads();
    uint64_t tmid = 0;
    for (int i = 0; i < opt.nthreads; i++)
        if (opt.threads[i] == 6)
            tmid = 6;
    const uint64_t tmin = opt.threads[0];
    uint64_t nmax = 0;
    for (int i = 0; i < opt.nkeys; i++)
        nmax = opt.keys[i] > nmax ? opt.keys[i] : nmax;
    char kb[32];

    fprintf(f, "# Experiment 6: threads at every key count\n\n## System\n\n");
    bench_print_system_info(f);
    fprintf(f, "\n## Configuration\n\n| Setting | Value |\n|---|---|\n| Layouts |");
    for (int i = 0; i < opt.nlayouts; i++)
        fprintf(f, "%s %s", i ? "," : "", layout_names[opt.layouts[i]]);
    fprintf(f, " |\n| Lock designs |");
    for (int i = 0; i < opt.ndesigns; i++)
        fprintf(f, "%s %s", i ? "," : "", design_names[opt.designs[i]]);
    fprintf(f, " |\n| Key counts |");
    for (int i = 0; i < opt.nkeys; i++) {
        fmt_keys(kb, sizeof(kb), opt.keys[i]);
        fprintf(f, "%s %s", i ? "," : "", kb);
    }
    fprintf(f, " (dense keys 0 .. N-1) |\n| Threads |");
    for (int i = 0; i < opt.nthreads; i++)
        fprintf(f, "%s %llu", i ? "," : "", (unsigned long long)opt.threads[i]);
    fprintf(f, " |\n| Workloads |");
    for (int i = 0; i < opt.nmixes; i++)
        fprintf(f, "%s %s = %s", i ? ";" : "", mix_names[opt.mixes[i]], mix_desc[opt.mixes[i]]);
    fprintf(f, " |\n| Key distributions |");
    for (int i = 0; i < opt.ndists; i++)
        fprintf(f, "%s %s", i ? "," : "", dist_names[opt.dists[i]]);
    fprintf(f, " (zipf: theta 0.99, hottest keys 0, 1, 2, ...) |\n| Operations per thread per workload | %llu |\n",
            (unsigned long long)opt.ops);
    fprintf(f, "| Repetitions | %llu (median throughput; latency percentiles over all samples, 1 in %llu ops timed) |\n",
            (unsigned long long)opt.reps, (unsigned long long)opt.sample);
    fprintf(f, "| Pages | %s |\n| Memory budget | %.2f GiB; hard stop 10 GiB; peak measured %.2f GiB |\n",
            page_names[opt.page_mode], gib(budget), gib(peak_measured));
    fprintf(f, "| CPU pinning | %s |\n", opt.ncpus ? "--cpus list" : "thread i on CPU i (Linux)");

    /* Memory plan */
    fprintf(f, "\n## Memory plan\n\n| keys | layout | memory (structure + load keys + operation lists + program) | "
               "decision |\n|---|---|---:|---|\n");
    for (size_t i = 0; i < nres; i++)
        if (res[i].design == -1) {
            fmt_keys(kb, sizeof(kb), res[i].n);
            fprintf(f, "| %s | %s | %.2f GiB | %s |\n", kb, layout_names[res[i].layout], gib(res[i].mem_plan),
                    res[i].reason);
        }
    if (opt.plan_only)
        return;

    /* Checks */
    fprintf(f, "\n## Checks\n\n| Design | torn / misrouted gets | wrong statuses | every key present after load |\n"
               "|---|---:|---:|---|\n");
    int all_ok = 1;
    for (int di = 0; di < opt.ndesigns; di++) {
        const int d = opt.designs[di];
        uint64_t bv = 0, bs = 0;
        int cnt = -1;
        for (size_t i = 0; i < nres; i++)
            if (res[i].ran && res[i].design == d) {
                bv += res[i].bad_value;
                bs += res[i].bad_status;
                if (res[i].count_ok != -1)
                    cnt = cnt == -1 ? res[i].count_ok : (cnt && res[i].count_ok);
            }
        all_ok &= bv == 0 && bs == 0 && cnt != 0;
        fprintf(f, "| %s | %llu %s | %llu %s | %s |\n", design_names[d], (unsigned long long)bv, bv ? "❌" : "✅",
                (unsigned long long)bs, bs ? "❌" : "✅", cnt == -1 ? "not run" : cnt ? "✅" : "❌");
    }
    fprintf(f, "\nOverall: %s\n", all_ok ? "✅ every operation returned the right result" : "❌ see above");

    /* Fastest design at the largest N and T */
    fmt_keys(kb, sizeof(kb), nmax);
    fprintf(f, "\n## Fastest design at %s keys, %llu threads (M ops/s)\n\n| workload |", kb, (unsigned long long)tmax);
    for (int li = 0; li < opt.nlayouts; li++)
        fprintf(f, " %s |", layout_names[opt.layouts[li]]);
    fprintf(f, "\n|---|");
    for (int li = 0; li < opt.nlayouts; li++)
        fprintf(f, "---|");
    fprintf(f, "\n");
    for (int w = 0; w < nwl; w++) {
        char lb[64];
        wl_label(lb, sizeof(lb), w);
        fprintf(f, "| %s |", lb);
        for (int li = 0; li < opt.nlayouts; li++) {
            const Result *best = NULL;
            for (int di = 0; di < opt.ndesigns; di++) {
                const Result *r = find(opt.layouts[li], opt.designs[di], nmax, tmax, wl_dist[w], wl_mix[w]);
                if (r != NULL && (best == NULL || r->mops > best->mops))
                    best = r;
            }
            if (best)
                fprintf(f, " %s (%.1f) |", design_names[best->design], best->mops);
            else
                fprintf(f, " - |");
        }
        fprintf(f, "\n");
    }

    /* Throughput vs key count */
    fprintf(f, "\n## Throughput vs key count (M ops/s)\n\nCell = %llu thread%s / ", (unsigned long long)tmin,
            tmin == 1 ? "" : "s");
    if (tmid)
        fprintf(f, "6 threads (one per physical core) / ");
    fprintf(f, "%llu threads. The question of experiment 6: does multithreaded throughput hold as the table grows "
               "from 1M to 100M keys?\n",
            (unsigned long long)tmax);
    for (int li = 0; li < opt.nlayouts; li++)
        for (int w = 0; w < nwl; w++) {
            char lb[64];
            wl_label(lb, sizeof(lb), w);
            fprintf(f, "\n### %s, %s\n\n| keys |", layout_names[opt.layouts[li]], lb);
            for (int di = 0; di < opt.ndesigns; di++)
                fprintf(f, " %s |", design_names[opt.designs[di]]);
            fprintf(f, "\n|---|");
            for (int di = 0; di < opt.ndesigns; di++)
                fprintf(f, "---:|");
            fprintf(f, "\n");
            for (int ki = 0; ki < opt.nkeys; ki++) {
                fmt_keys(kb, sizeof(kb), opt.keys[ki]);
                fprintf(f, "| %s |", kb);
                for (int di = 0; di < opt.ndesigns; di++) {
                    const uint64_t ts[3] = {tmin, tmid, tmax};
                    char cell[96];
                    size_t pos = 0;
                    cell[0] = '\0';
                    int any = 0;
                    for (int k = 0; k < 3; k++) {
                        if (ts[k] == 0 || (k > 0 && ts[k] == ts[k - 1]) || (k == 1 && ts[1] == tmax))
                            continue;
                        const Result *r = find(opt.layouts[li], opt.designs[di], opt.keys[ki], ts[k], wl_dist[w],
                                               wl_mix[w]);
                        pos += (size_t)snprintf(cell + pos, sizeof(cell) - pos, "%s%s", pos ? " / " : "", "");
                        if (r) {
                            pos += (size_t)snprintf(cell + pos, sizeof(cell) - pos, "%.1f", r->mops);
                            any = 1;
                        } else {
                            pos += (size_t)snprintf(cell + pos, sizeof(cell) - pos, "-");
                        }
                    }
                    fprintf(f, " %s |", any ? cell : "skipped");
                }
                fprintf(f, "\n");
            }
        }

    /* Scaling with threads at the largest N */
    fmt_keys(kb, sizeof(kb), nmax);
    fprintf(f, "\n## Scaling with threads at %s keys (M ops/s)\n", kb);
    for (int li = 0; li < opt.nlayouts; li++)
        for (int w = 0; w < nwl; w++) {
            char lb[64];
            wl_label(lb, sizeof(lb), w);
            fprintf(f, "\n### %s, %s\n\n| threads |", layout_names[opt.layouts[li]], lb);
            for (int di = 0; di < opt.ndesigns; di++)
                fprintf(f, " %s |", design_names[opt.designs[di]]);
            fprintf(f, "\n|---|");
            for (int di = 0; di < opt.ndesigns; di++)
                fprintf(f, "---:|");
            fprintf(f, "\n");
            for (int ti = 0; ti < opt.nthreads; ti++) {
                fprintf(f, "| %llu |", (unsigned long long)opt.threads[ti]);
                for (int di = 0; di < opt.ndesigns; di++) {
                    const Result *r =
                        find(opt.layouts[li], opt.designs[di], nmax, opt.threads[ti], wl_dist[w], wl_mix[w]);
                    if (r)
                        fprintf(f, " %.1f |", r->mops);
                    else
                        fprintf(f, " - |");
                }
                fprintf(f, "\n");
            }
        }

    /* Flat vs rows */
    if (opt.nlayouts >= 2) {
        const int la = opt.layouts[0], lb_ = opt.layouts[1];
        fprintf(f, "\n## %s vs %s at %llu threads (M ops/s, ratio)\n", layout_names[lb_], layout_names[la],
                (unsigned long long)tmax);
        for (int w = 0; w < nwl; w++) {
            char lb[64];
            wl_label(lb, sizeof(lb), w);
            fprintf(f, "\n**%s**\n\n| keys |", lb);
            for (int di = 0; di < opt.ndesigns; di++)
                fprintf(f, " %s |", design_names[opt.designs[di]]);
            fprintf(f, "\n|---|");
            for (int di = 0; di < opt.ndesigns; di++)
                fprintf(f, "---:|");
            fprintf(f, "\n");
            for (int ki = 0; ki < opt.nkeys; ki++) {
                fmt_keys(kb, sizeof(kb), opt.keys[ki]);
                fprintf(f, "| %s |", kb);
                for (int di = 0; di < opt.ndesigns; di++) {
                    const Result *a = find(la, opt.designs[di], opt.keys[ki], tmax, wl_dist[w], wl_mix[w]);
                    const Result *b = find(lb_, opt.designs[di], opt.keys[ki], tmax, wl_dist[w], wl_mix[w]);
                    if (a && b && a->mops > 0)
                        fprintf(f, " %.1f / %.1f (%.2fx) |", b->mops, a->mops, b->mops / a->mops);
                    else
                        fprintf(f, " - |");
                }
                fprintf(f, "\n");
            }
        }
    }

    /* Tail latency */
    fprintf(f, "\n## p99 latency at %llu threads (ns)\n", (unsigned long long)tmax);
    for (int li = 0; li < opt.nlayouts; li++)
        for (int w = 0; w < nwl; w++) {
            char lb[64];
            wl_label(lb, sizeof(lb), w);
            fprintf(f, "\n### %s, %s\n\n| keys |", layout_names[opt.layouts[li]], lb);
            for (int di = 0; di < opt.ndesigns; di++)
                fprintf(f, " %s p99 (p99.9) |", design_names[opt.designs[di]]);
            fprintf(f, "\n|---|");
            for (int di = 0; di < opt.ndesigns; di++)
                fprintf(f, "---:|");
            fprintf(f, "\n");
            for (int ki = 0; ki < opt.nkeys; ki++) {
                fmt_keys(kb, sizeof(kb), opt.keys[ki]);
                fprintf(f, "| %s |", kb);
                for (int di = 0; di < opt.ndesigns; di++) {
                    const Result *r = find(opt.layouts[li], opt.designs[di], opt.keys[ki], tmax, wl_dist[w], wl_mix[w]);
                    if (r)
                        fprintf(f, " %.0f (%.0f) |", r->p99, r->p999);
                    else
                        fprintf(f, " - |");
                }
                fprintf(f, "\n");
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
        fprintf(f, "layout,design,page_mode,keys,threads,dist,mix,mops,p50_ns,p99_ns,p999_ns,bad_value,bad_status,"
                   "count_ok,mem_plan_bytes,mem_measured_bytes\n");
    for (size_t i = 0; i < nres; i++) {
        const Result *r = &res[i];
        if (!r->ran)
            continue;
        fprintf(f, "%s,%s,%s,%llu,%llu,%s,%s,%.3f,%.0f,%.0f,%.0f,%llu,%llu,%d,%llu,%llu\n", layout_names[r->layout],
                design_names[r->design], page_names[opt.page_mode], (unsigned long long)r->n,
                (unsigned long long)r->threads, r->dist < 0 ? "-" : dist_names[r->dist], mix_names[r->mix], r->mops,
                r->p50, r->p99, r->p999, (unsigned long long)r->bad_value, (unsigned long long)r->bad_status,
                r->count_ok, (unsigned long long)r->mem_plan, (unsigned long long)r->mem_measured);
    }
    fclose(f);
}

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    if (parse_args(argc, argv) != 0) {
        usage(stderr);
        return 2;
    }
    build_workloads();

    budget = (uint64_t)(opt.budget_gib * (double)GIB);
    const long avail_kib = bench_meminfo_value("MemAvailable");
    if (opt.page_mode == VDC_PAGES_BASE && avail_kib > 0) {
        const uint64_t avail = (uint64_t)avail_kib * 1024u;
        if (avail > GIB / 2 && avail - GIB / 2 < budget) {
            fprintf(stderr, "note: only %.2f GiB available; budget lowered to %.2f GiB\n", gib(avail),
                    gib(avail - GIB / 2));
            budget = avail - GIB / 2;
        }
    }
    const long huge_free = bench_meminfo_value("HugePages_Free");
    if (opt.page_mode == VDC_PAGES_HUGETLB && !opt.plan_only) {
        void *p = vdc_map(VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB, 1);
        if (p == NULL) {
            fprintf(stderr, "ERROR: no 2 MiB hugetlb pages available (%s). Use scripts/scale/run_mt_scale.sh, which "
                            "reserves them, or --pages base.\n",
                    strerror(errno));
            return 2;
        }
        vdc_unmap(p, VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB);
    }

    /* Plan every (N, layout) first; skipped ones are recorded with design = -1. */
    int runnable[MAX_LIST][NLAYOUTS];
    for (int ki = 0; ki < opt.nkeys; ki++)
        for (int li = 0; li < opt.nlayouts; li++) {
            const int L = opt.layouts[li];
            const uint64_t n = opt.keys[ki];
            Result r;
            memset(&r, 0, sizeof(r));
            r.layout = L;
            r.design = -1;
            r.n = n;
            r.mem_plan = plan_total(L, n);
            const uint64_t pages = huge_pages_of(L, n);
            runnable[ki][li] = 0;
            if (r.mem_plan > budget)
                snprintf(r.reason, sizeof(r.reason), "⏭ skipped: needs %.2f GiB, budget %.2f GiB", gib(r.mem_plan),
                         gib(budget));
            else if (opt.page_mode == VDC_PAGES_HUGETLB && huge_free >= 0 && pages > (uint64_t)huge_free)
                snprintf(r.reason, sizeof(r.reason), "⏭ skipped: needs %llu huge pages, %ld free",
                         (unsigned long long)pages, huge_free);
            else {
                snprintf(r.reason, sizeof(r.reason), "✅ %s: %.2f GiB of %.2f GiB",
                         opt.plan_only ? "will run" : "ran", gib(r.mem_plan), gib(budget));
                runnable[ki][li] = 1;
            }
            add_res(&r);
        }
    if (opt.plan_only) {
        print_report();
        return 0;
    }

    fprintf(stderr, "== experiment 6: threads at every key count (budget %.2f GiB, %s pages)\n", gib(budget),
            page_names[opt.page_mode]);
    for (int ki = 0; ki < opt.nkeys; ki++) {
        const uint64_t n = opt.keys[ki];
        int any = 0;
        for (int li = 0; li < opt.nlayouts; li++)
            any |= runnable[ki][li];
        if (!any)
            continue;
        char kb[32];
        fmt_keys(kb, sizeof(kb), n);

        /* Shared inputs for this N: random insertion order, zipf constants. */
        const uint64_t tp = bench_now_ns();
        uint32_t *load_keys = malloc(n * sizeof(uint32_t));
        if (load_keys == NULL) {
            fprintf(stderr, "error: out of memory for load keys\n");
            return 2;
        }
        DatPerm perm;
        dat_perm_init(&perm, n, opt.seed);
        for (uint64_t i = 0; i < n; i++)
            load_keys[i] = (uint32_t)dat_perm(&perm, i);
        Zipf z;
        zipf_init(&z, n, 0.99);
        fprintf(stderr, "  %s keys: inputs ready in %.1f s\n", kb, (double)(bench_now_ns() - tp) / 1e9);

        for (int ti = 0; ti < opt.nthreads; ti++) {
            const uint64_t T = opt.threads[ti];
            make_work(n, T, &z);
            for (int li = 0; li < opt.nlayouts; li++) {
                if (!runnable[ki][li])
                    continue;
                const int L = opt.layouts[li];
                for (int di = 0; di < opt.ndesigns; di++) {
                    const int D = opt.designs[di];
                    DatConc c;
                    const int rc = dc_init(&c, layout_bits[L] ? DC_ROWS : DC_FLAT, (DcMode)D, layout_bits[L], n, 0,
                                           (VdcPageMode)opt.page_mode);
                    if (rc != MT_OK) {
                        fprintf(stderr, "  %s %s %s: could not allocate (%s), skipped\n", layout_names[L],
                                design_names[D], kb, strerror(errno));
                        continue;
                    }
                    const uint64_t now = measured_bytes();
                    peak_measured = now > peak_measured ? now : peak_measured;
                    if (now > HARD_LIMIT) {
                        fprintf(stderr, "FATAL: process memory %.2f GiB exceeds the 10 GiB hard limit; stopping\n",
                                gib(now));
                        dc_destroy(&c);
                        return 3;
                    }
                    int loaded = 0;
                    for (int w = 0; w < nwl; w++) {
                        if (wl_mix[w] == MIX_LOAD) {
                            run_cell(&c, L, D, n, T, -1, load_keys, plan_total(L, n));
                            loaded = 1;
                            continue;
                        }
                        if (!loaded) { /* no load workload requested: fill untimed */
                            uint64_t v[4] = {0, 0, 0, 0};
                            for (uint64_t k = 0; k < n; k++) {
                                v[0] = v[1] = v[2] = v[3] = (uint64_t)k << 32;
                                dc_put(&c, (uint32_t)k, v, 32, 0);
                            }
                            loaded = 1;
                        }
                        int combo = -1;
                        for (int cc = 0; cc < ncombos; cc++)
                            if (combo_mix[cc] == wl_mix[w] && combo_dist[cc] == wl_dist[w])
                                combo = cc;
                        run_cell(&c, L, D, n, T, combo, load_keys, plan_total(L, n));
                    }
                    dc_destroy(&c);
                }
            }
            free_work(T);
        }
        free(load_keys);
    }

    print_report();
    if (opt.csv != NULL)
        write_csv();
    int failed = 0;
    for (size_t i = 0; i < nres; i++)
        failed |= res[i].ran && (res[i].bad_value || res[i].bad_status || res[i].count_ok == 0);
    if (failed)
        fprintf(stderr, "ERROR: wrong results detected (see the Checks table)\n");
    free(res);
    return failed ? 1 : 0;
}
