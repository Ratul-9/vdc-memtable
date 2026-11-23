/*
 * vdc_mtbench - multithreaded benchmark of the VDC memtable designs.
 *
 * Variants
 *   direct (every thread calls the memtable):  mutex, spin, rwlock, nodelock, seqlock
 *   delegation (producers -> queues -> key owners):
 *     mpsc-sync / mpsc-async         1 consumer owns all keys
 *     sharded-sync / sharded-async   C consumers, one MPSC queue each, key ranges
 *     mpmc-sync / mpmc-async         C consumers share one MPMC queue, forward foreign keys
 *   sync  = each producer waits for every result (round trip);
 *   async = each producer keeps up to --window requests in flight.
 *
 * Thread budget: "threads" = all threads of the run, consumers included.
 *   direct: T workers.  mpsc: 1 consumer + T-1 producers.
 *   sharded / mpmc: C = --consumers (default T/2) consumers + T-C producers.
 *
 * Workloads: load (insert all keys into an empty table), A (50% get / 50% update),
 * B (95/5), C (100% get), W (100% update). Key distributions: uniform, zipf
 * (theta 0.99, hottest keys are 0, 1, 2, ...), interleaved (op i of worker w
 * uses key i*W + w: all workers walk neighbouring keys at the same time, like
 * auto-increment ids). Values: 32 B, four equal 64-bit words containing the key,
 * so every get is checked for torn or misrouted results.
 *
 * Output: markdown tables on stdout (throughput, latency percentiles, scaling,
 * VDC scatter vs identity), progress on stderr, optional CSV.
 */
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_common.h"
#include "vdc/concurrent.h"
#include "vdc/service.h"

enum {
    V_MUTEX, V_SPIN, V_RWLOCK, V_NODELOCK, V_SEQLOCK,
    V_MPSC_SYNC, V_MPSC_ASYNC, V_SHARDED_SYNC, V_SHARDED_ASYNC, V_MPMC_SYNC, V_MPMC_ASYNC,
    V_COUNT
};
static const char *const vnames[V_COUNT] = {"mutex", "spin", "rwlock", "nodelock", "seqlock",
                                            "mpsc-sync", "mpsc-async", "sharded-sync", "sharded-async",
                                            "mpmc-sync", "mpmc-async"};

enum { MIX_LOAD, MIX_A, MIX_B, MIX_C, MIX_W, MIX_COUNT };
static const char *const mixnames[MIX_COUNT] = {"load", "A", "B", "C", "W"};
static const int mix_read_pct[MIX_COUNT] = {0, 50, 95, 100, 0};
static const char *const mixdesc[MIX_COUNT] = {"insert every key into an empty table", "50% get / 50% update",
                                               "95% get / 5% update", "100% get", "100% update"};

enum { D_UNIFORM, D_ZIPF, D_INTERLEAVED, D_COUNT };
static const char *const distnames[D_COUNT] = {"uniform", "zipf", "interleaved"};

#define MAX_LIST 16
#define MAX_THREADS 256

static int is_direct(int v) { return v <= V_SEQLOCK; }
static int is_async(int v) { return v == V_MPSC_ASYNC || v == V_SHARDED_ASYNC || v == V_MPMC_ASYNC; }
static SvMode sv_mode_of(int v)
{
    return (v <= V_MPSC_ASYNC) ? SV_MPSC : (v <= V_SHARDED_ASYNC) ? SV_SHARDED : SV_SHARED_MPMC;
}

/* ---------------------------------------------------------------- options */

typedef struct Options {
    int variants[V_COUNT];
    int nvariants;
    uint64_t threads[MAX_LIST];
    int nthreads;
    uint64_t js[MAX_LIST];
    int nj;
    int mixes[MIX_COUNT];
    int nmixes;
    int dists[D_COUNT];
    int ndists;
    uint64_t keys;
    uint64_t ops;
    uint64_t reps;
    uint64_t window;
    uint64_t consumers; /* 0 = T/2 */
    uint64_t sample;
    uint64_t queue_cap;
    int page_mode;
    uint64_t cpus[MAX_THREADS];
    int ncpus;
    const char *csv;
    uint64_t seed;
} Options;

static Options opt;

static void usage(FILE *f)
{
    fprintf(f,
            "Usage: vdc_mtbench [options]\n"
            "  --variants LIST   mutex,spin,rwlock,nodelock,seqlock,mpsc-sync,mpsc-async,\n"
            "                    sharded-sync,sharded-async,mpmc-sync,mpmc-async   (default all)\n"
            "  --threads LIST    total threads per run, consumers included  (default 1,2,4,6,8,12)\n"
            "  --consumers N     consumers for sharded / mpmc (default T/2)\n"
            "  --j LIST          key -> slot mapping                        (default 6,11,20)\n"
            "  --mixes LIST      load,A,B,C,W                               (default all)\n"
            "  --dists LIST      uniform,zipf,interleaved                   (default all)\n"
            "  --keys N          key count, max 1048576                     (default 1M)\n"
            "  --ops N           operations per worker / producer           (default 500K)\n"
            "  --reps N          repetitions, median reported               (default 3)\n"
            "  --window N        async requests in flight per producer      (default 32)\n"
            "  --queue N         queue capacity, power of two               (default 4096)\n"
            "  --sample N        time 1 of every N operations for latency   (default 64)\n"
            "  --pages base|hugetlb                                         (default base)\n"
            "  --cpus LIST       CPU for thread 0, 1, ... (consumers first)  (default 0,1,2,...)\n"
            "  --csv FILE        append results as CSV\n"
            "  --seed N          RNG seed                                   (default 42)\n");
}

static int parse_args(int argc, char **argv)
{
    memset(&opt, 0, sizeof(opt));
    for (int i = 0; i < V_COUNT; i++)
        opt.variants[i] = i;
    opt.nvariants = V_COUNT;
    const uint64_t th[] = {1, 2, 4, 6, 8, 12};
    memcpy(opt.threads, th, sizeof(th));
    opt.nthreads = 6;
    const uint64_t js[] = {6, 11, 20};
    memcpy(opt.js, js, sizeof(js));
    opt.nj = 3;
    for (int i = 0; i < MIX_COUNT; i++)
        opt.mixes[i] = i;
    opt.nmixes = MIX_COUNT;
    for (int i = 0; i < D_COUNT; i++)
        opt.dists[i] = i;
    opt.ndists = D_COUNT;
    opt.keys = 1000000;
    opt.ops = 500000;
    opt.reps = 3;
    opt.window = 32;
    opt.sample = 64;
    opt.queue_cap = 4096;
    opt.seed = 42;
    static const char *const pages[] = {"base", "hugetlb"};

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            exit(0);
        }
        const char *v = (i + 1 < argc) ? argv[++i] : NULL;
        int n = 0;
        if (v == NULL) {
            fprintf(stderr, "error: missing value for %s\n", a);
            return -1;
        }
        if (!strcmp(a, "--variants"))
            n = opt.nvariants = bench_parse_name_list(v, vnames, V_COUNT, opt.variants, V_COUNT);
        else if (!strcmp(a, "--threads"))
            n = opt.nthreads = bench_parse_u64_list(v, opt.threads, MAX_LIST);
        else if (!strcmp(a, "--j"))
            n = opt.nj = bench_parse_u64_list(v, opt.js, MAX_LIST);
        else if (!strcmp(a, "--mixes"))
            n = opt.nmixes = bench_parse_name_list(v, mixnames, MIX_COUNT, opt.mixes, MIX_COUNT);
        else if (!strcmp(a, "--dists"))
            n = opt.ndists = bench_parse_name_list(v, distnames, D_COUNT, opt.dists, D_COUNT);
        else if (!strcmp(a, "--keys"))
            n = bench_parse_u64_list(v, &opt.keys, 1);
        else if (!strcmp(a, "--ops"))
            n = bench_parse_u64_list(v, &opt.ops, 1);
        else if (!strcmp(a, "--reps"))
            n = bench_parse_u64_list(v, &opt.reps, 1);
        else if (!strcmp(a, "--window"))
            n = bench_parse_u64_list(v, &opt.window, 1);
        else if (!strcmp(a, "--consumers"))
            n = bench_parse_u64_list(v, &opt.consumers, 1);
        else if (!strcmp(a, "--sample"))
            n = bench_parse_u64_list(v, &opt.sample, 1);
        else if (!strcmp(a, "--queue"))
            n = bench_parse_u64_list(v, &opt.queue_cap, 1);
        else if (!strcmp(a, "--seed"))
            n = bench_parse_u64_list(v, &opt.seed, 1);
        else if (!strcmp(a, "--cpus"))
            n = opt.ncpus = bench_parse_u64_list(v, opt.cpus, MAX_THREADS);
        else if (!strcmp(a, "--csv")) {
            opt.csv = v;
            n = 1;
        } else if (!strcmp(a, "--pages")) {
            int pm;
            n = bench_parse_name_list(v, pages, 2, &pm, 1);
            opt.page_mode = pm;
        } else {
            fprintf(stderr, "error: unknown option %s\n", a);
            return -1;
        }
        if (n <= 0) {
            fprintf(stderr, "error: bad value for %s: %s\n", a, v);
            return -1;
        }
    }
    if (opt.keys == 0 || opt.keys > VDC_NUM_SLOTS || opt.ops == 0 || opt.reps == 0 || opt.reps > 50 ||
        opt.window == 0 || opt.window > 1024 || opt.sample == 0 || opt.queue_cap < 2 ||
        (opt.queue_cap & (opt.queue_cap - 1))) {
        fprintf(stderr, "error: need keys 1..2^20, ops > 0, reps 1..50, window 1..1024, queue a power of two\n");
        return -1;
    }
    for (int i = 0; i < opt.nthreads; i++)
        if (opt.threads[i] == 0 || opt.threads[i] > MAX_THREADS) {
            fprintf(stderr, "error: threads must be 1..%d\n", MAX_THREADS);
            return -1;
        }
    for (int i = 0; i < opt.nj; i++)
        if (opt.js[i] > VDC_KEY_BITS) {
            fprintf(stderr, "error: j must be 0..20\n");
            return -1;
        }
    return 0;
}

static int cpu_of(int idx)
{
    if (opt.ncpus > 0)
        return idx < opt.ncpus ? (int)opt.cpus[idx] : -1;
    return idx;
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
    const double zeta2 = 1.0 + pow(0.5, theta);
    z->n = (double)n;
    z->theta = theta;
    z->alpha = 1.0 / (1.0 - theta);
    z->zetan = zetan;
    z->eta = (1.0 - pow(2.0 / (double)n, 1.0 - theta)) / (1.0 - zeta2 / zetan);
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
    if (k >= (uint64_t)z->n)
        k = (uint64_t)z->n - 1;
    return (uint32_t)k;
}

enum { OPK_PUT = 0, OPK_GET = 1 };

typedef struct Work {
    uint32_t *keys;
    uint8_t *ops;
    size_t n;
} Work;

static uint32_t *g_perm; /* random permutation of the key set, for loads */
static Zipf g_zipf;

static void work_free(Work *w)
{
    free(w->keys);
    free(w->ops);
    memset(w, 0, sizeof(*w));
}

/* Operation list of worker `wi` out of `nw`. */
static int work_make(Work *w, int mix, int dist, int wi, int nw, uint64_t seed)
{
    const uint64_t nkeys = opt.keys;
    BenchRng r = {seed * 0x9E3779B97F4A7C15ull + (uint64_t)wi * 7919u + (uint64_t)mix * 131u + (uint64_t)dist};
    if (mix == MIX_LOAD)
        w->n = (size_t)((nkeys - (uint64_t)wi + (uint64_t)nw - 1) / (uint64_t)nw);
    else
        w->n = (size_t)opt.ops;
    w->keys = malloc(w->n * sizeof(uint32_t));
    w->ops = malloc(w->n);
    if (w->keys == NULL || w->ops == NULL)
        return -1;

    for (size_t i = 0; i < w->n; i++) {
        if (mix == MIX_LOAD) {
            const uint64_t idx = (uint64_t)i * (uint64_t)nw + (uint64_t)wi; /* worker's share, round robin */
            w->keys[i] = dist == D_INTERLEAVED ? (uint32_t)idx : g_perm[idx];
            w->ops[i] = OPK_PUT;
            continue;
        }
        uint32_t key;
        if (dist == D_UNIFORM)
            key = (uint32_t)bench_rng_below(&r, nkeys);
        else if (dist == D_ZIPF)
            key = zipf_next(&g_zipf, &r);
        else
            key = (uint32_t)(((uint64_t)i * (uint64_t)nw + (uint64_t)wi) % nkeys);
        w->keys[i] = key;
        w->ops[i] = (bench_rng_below(&r, 100) < (uint64_t)mix_read_pct[mix]) ? OPK_GET : OPK_PUT;
    }
    return 0;
}

/* ---------------------------------------------------------------- run one cell */

typedef struct Ctx {
    int variant;
    int cpu;
    CMemtable *cm;
    VdcService *sv;
    Work *work;
    uint64_t seq_base;
    /* results */
    uint64_t t_end;
    uint64_t ops;
    uint64_t bad_value; /* torn / misrouted get results */
    uint64_t bad_status;
    double *lat;
    size_t nlat;
} Ctx;

static int g_ready, g_go;

static void make_value(uint64_t w[4], uint32_t key, uint64_t seq)
{
    const uint64_t x = ((uint64_t)key << 32) | (seq & 0xffffffffu);
    w[0] = w[1] = w[2] = w[3] = x;
}

static void check_get(Ctx *c, int rc, const uint64_t w[4], uint32_t len, uint32_t key)
{
    if (rc != MT_OK)
        c->bad_status++;
    else if (len != 32 || w[0] != w[1] || w[1] != w[2] || w[2] != w[3] || (uint32_t)(w[0] >> 32) != key)
        c->bad_value++;
}

static void run_direct(Ctx *c)
{
    uint64_t w[4];
    uint64_t seq = c->seq_base;
    const Work *wk = c->work;
    for (size_t i = 0; i < wk->n; i++) {
        const uint32_t key = wk->keys[i];
        const int sample = (i % opt.sample) == 0;
        const uint64_t t0 = sample ? bench_now_ns() : 0;
        if (wk->ops[i] == OPK_PUT) {
            make_value(w, key, ++seq);
            if (cm_put(c->cm, key, w, 32, seq) != MT_OK)
                c->bad_status++;
        } else {
            uint32_t len = 0;
            const int rc = cm_get(c->cm, key, w, &len, NULL);
            check_get(c, rc, w, len, key);
        }
        if (sample)
            c->lat[c->nlat++] = (double)(bench_now_ns() - t0);
    }
    c->ops = wk->n;
}

static void run_sync(Ctx *c)
{
    uint64_t w[4];
    uint64_t seq = c->seq_base;
    const Work *wk = c->work;
    for (size_t i = 0; i < wk->n; i++) {
        const uint32_t key = wk->keys[i];
        const int sample = (i % opt.sample) == 0;
        const uint64_t t0 = sample ? bench_now_ns() : 0;
        if (wk->ops[i] == OPK_PUT) {
            make_value(w, key, ++seq);
            if (sv_put(c->sv, key, w, 32, seq) != MT_OK)
                c->bad_status++;
        } else {
            uint32_t len = 0;
            const int rc = sv_get(c->sv, key, w, &len, NULL);
            check_get(c, rc, w, len, key);
        }
        if (sample)
            c->lat[c->nlat++] = (double)(bench_now_ns() - t0);
    }
    c->ops = wk->n;
}

typedef struct Slot {
    uint32_t key;
    uint8_t op;
    uint8_t busy;
    uint64_t t0;
} Slot;

static void async_finish(Ctx *c, VdcCompletion *comp, Slot *s)
{
    while (!__atomic_load_n(&comp->done, __ATOMIC_ACQUIRE))
        vdc_cpu_relax();
    if (s->op == OPK_GET) {
        uint64_t w[4];
        memcpy(w, comp->value, sizeof(w));
        check_get(c, comp->status, w, comp->len, s->key);
    } else if (comp->status != MT_OK) {
        c->bad_status++;
    }
    if (s->t0)
        c->lat[c->nlat++] = (double)(bench_now_ns() - s->t0);
    s->busy = 0;
}

static void run_async(Ctx *c)
{
    const size_t K = (size_t)opt.window;
    VdcCompletion *comp = aligned_alloc(64, K * sizeof(VdcCompletion));
    Slot *slots = calloc(K, sizeof(Slot));
    if (comp == NULL || slots == NULL) {
        c->bad_status++;
        free(comp);
        free(slots);
        return;
    }
    memset(comp, 0, K * sizeof(VdcCompletion));
    uint64_t seq = c->seq_base;
    const Work *wk = c->work;
    VdcRequest r;
    memset(&r, 0, sizeof(r));

    for (size_t i = 0; i < wk->n; i++) {
        const uint32_t key = wk->keys[i];
        const size_t si = i % K;
        if (slots[si].busy)
            async_finish(c, &comp[si], &slots[si]);
        /* Never two requests on one key in flight (keeps per-key order in every design). */
        for (size_t k = 0; k < K; k++)
            if (slots[k].busy && slots[k].key == key)
                async_finish(c, &comp[k], &slots[k]);

        r.key = key;
        r.done = &comp[si];
        if (wk->ops[i] == OPK_PUT) {
            uint64_t w[4];
            make_value(w, key, ++seq);
            r.op = VDC_OP_PUT;
            r.len = 32;
            r.seqno = seq;
            memcpy(r.value, w, sizeof(w));
        } else {
            r.op = VDC_OP_GET;
            r.len = 0;
        }
        slots[si].key = key;
        slots[si].op = wk->ops[i];
        slots[si].busy = 1;
        slots[si].t0 = (i % opt.sample) == 0 ? bench_now_ns() : 0;
        comp[si].done = 0;
        if (sv_submit(c->sv, &r) != MT_OK)
            c->bad_status++;
    }
    for (size_t k = 0; k < K; k++)
        if (slots[k].busy)
            async_finish(c, &comp[k], &slots[k]);
    c->ops = wk->n;
    free(comp);
    free(slots);
}

static void *worker_main(void *arg)
{
    Ctx *c = (Ctx *)arg;
    if (c->cpu >= 0)
        (void)vdc_pin_thread(c->cpu);
    __atomic_fetch_add(&g_ready, 1, __ATOMIC_ACQ_REL);
    while (!__atomic_load_n(&g_go, __ATOMIC_ACQUIRE))
        vdc_cpu_relax();
    if (is_direct(c->variant))
        run_direct(c);
    else if (is_async(c->variant))
        run_async(c);
    else
        run_sync(c);
    c->t_end = bench_now_ns();
    return NULL;
}

typedef struct Result {
    int variant, dist, mix, consumers, producers;
    uint64_t j, threads;
    double mops;
    double p50, p99, p999; /* ns */
    uint64_t bad_value, bad_status;
    int count_ok; /* load only: every key present afterwards (1), wrong (0), not checked (-1) */
    uint64_t forwarded;
} Result;

static Result *results;
static size_t nresults, capresults;

static void add_result(const Result *r)
{
    if (nresults == capresults) {
        capresults = capresults ? capresults * 2 : 256;
        results = realloc(results, capresults * sizeof(*results));
        if (results == NULL) {
            fprintf(stderr, "error: out of memory\n");
            exit(2);
        }
    }
    results[nresults++] = *r;
}

static double pct(double *v, size_t n, double p)
{
    if (n == 0)
        return 0.0;
    size_t i = (size_t)(p * (double)(n - 1) + 0.5);
    return v[i];
}

static int cmp_d(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Untimed fill of all keys so the mixes run on a full table. */
static void preload(int variant, CMemtable *cm, VdcService *sv)
{
    uint64_t w[4];
    for (uint64_t k = 0; k < opt.keys; k++) {
        make_value(w, (uint32_t)k, 0);
        if (is_direct(variant)) {
            cm_put(cm, (uint32_t)k, w, 32, 0);
        } else {
            VdcRequest r;
            memset(&r, 0, sizeof(r));
            r.op = VDC_OP_PUT;
            r.key = (uint32_t)k;
            r.len = 32;
            memcpy(r.value, w, sizeof(w));
            sv_submit(sv, &r);
        }
    }
    if (sv != NULL)
        sv_quiesce(sv);
}

static void clear_table(CMemtable *cm, VdcService *sv)
{
    if (cm != NULL)
        cm_clear(cm);
    else
        sv_clear(sv);
}

static uint32_t count_table(CMemtable *cm, VdcService *sv)
{
    return cm != NULL ? cm_count(cm, NULL) : sv_count(sv, NULL);
}

static void run_cell(int variant, uint64_t j, uint64_t T, int dist, int mix, CMemtable *cm, VdcService *sv,
                     int consumers)
{
    const int nw = is_direct(variant) ? (int)T : (int)T - consumers;
    Ctx *ctx = calloc((size_t)nw, sizeof(Ctx));
    Work *work = calloc((size_t)nw, sizeof(Work));
    pthread_t *th = calloc((size_t)nw, sizeof(pthread_t));
    double *mops = calloc(opt.reps, sizeof(double));
    if (ctx == NULL || work == NULL || th == NULL || mops == NULL) {
        fprintf(stderr, "error: out of memory\n");
        exit(2);
    }
    for (int i = 0; i < nw; i++)
        if (work_make(&work[i], mix, dist, i, nw, opt.seed) != 0) {
            fprintf(stderr, "error: out of memory\n");
            exit(2);
        }

    size_t lat_cap = 0;
    for (int i = 0; i < nw; i++)
        lat_cap += work[i].n / opt.sample + 1;
    double *lat_all = malloc(lat_cap * opt.reps * sizeof(double));
    size_t nlat_all = 0;
    uint64_t bad_value = 0, bad_status = 0;
    int count_ok = -1;

    for (uint64_t rep = 0; rep < opt.reps; rep++) {
        if (mix == MIX_LOAD)
            clear_table(cm, sv);
        g_ready = 0;
        g_go = 0;
        for (int i = 0; i < nw; i++) {
            ctx[i] = (Ctx){0};
            ctx[i].variant = variant;
            ctx[i].cpu = cpu_of(is_direct(variant) ? i : consumers + i);
            ctx[i].cm = cm;
            ctx[i].sv = sv;
            ctx[i].work = &work[i];
            ctx[i].seq_base = ((uint64_t)(rep + 1) << 40) + ((uint64_t)i << 32);
            ctx[i].lat = lat_all + nlat_all;
            nlat_all += work[i].n / opt.sample + 1; /* reserve */
            if (pthread_create(&th[i], NULL, worker_main, &ctx[i]) != 0) {
                fprintf(stderr, "error: cannot create thread\n");
                exit(2);
            }
        }
        while (__atomic_load_n(&g_ready, __ATOMIC_ACQUIRE) < nw)
            vdc_cpu_relax();
        const uint64_t t0 = bench_now_ns();
        __atomic_store_n(&g_go, 1, __ATOMIC_RELEASE);
        uint64_t t1 = t0, ops = 0;
        for (int i = 0; i < nw; i++) {
            pthread_join(th[i], NULL);
            if (ctx[i].t_end > t1)
                t1 = ctx[i].t_end;
            ops += ctx[i].ops;
            bad_value += ctx[i].bad_value;
            bad_status += ctx[i].bad_status;
        }
        mops[rep] = t1 > t0 ? (double)ops / ((double)(t1 - t0) / 1e3) : 0.0;

        /* Compact the latency samples actually recorded this rep. */
        size_t base = nlat_all;
        for (int i = nw - 1; i >= 0; i--)
            base -= work[i].n / opt.sample + 1;
        size_t pos = base;
        for (int i = 0; i < nw; i++) {
            memmove(lat_all + pos, ctx[i].lat, ctx[i].nlat * sizeof(double));
            pos += ctx[i].nlat;
        }
        nlat_all = pos;

        if (mix == MIX_LOAD) {
            if (sv != NULL)
                sv_quiesce(sv);
            const int ok = count_table(cm, sv) == (uint32_t)opt.keys;
            count_ok = (count_ok == -1) ? ok : (count_ok && ok);
        }
    }

    qsort(lat_all, nlat_all, sizeof(double), cmp_d);
    Result r;
    memset(&r, 0, sizeof(r));
    r.variant = variant;
    r.j = j;
    r.threads = T;
    r.dist = dist;
    r.mix = mix;
    r.consumers = is_direct(variant) ? 0 : consumers;
    r.producers = nw;
    r.mops = bench_median(mops, opt.reps);
    r.p50 = pct(lat_all, nlat_all, 0.50);
    r.p99 = pct(lat_all, nlat_all, 0.99);
    r.p999 = pct(lat_all, nlat_all, 0.999);
    r.bad_value = bad_value;
    r.bad_status = bad_status;
    r.count_ok = count_ok;
    r.forwarded = sv != NULL ? sv_forwarded(sv) : 0;
    add_result(&r);

    for (int i = 0; i < nw; i++)
        work_free(&work[i]);
    free(ctx);
    free(work);
    free(th);
    free(mops);
    free(lat_all);
}

/* ---------------------------------------------------------------- report */

static const Result *find(int v, uint64_t j, uint64_t T, int d, int m)
{
    for (size_t i = 0; i < nresults; i++) {
        const Result *r = &results[i];
        if (r->variant == v && r->j == j && r->threads == T && r->dist == d && r->mix == m)
            return r;
    }
    return NULL;
}

static uint64_t max_threads(void)
{
    uint64_t m = 0;
    for (int i = 0; i < opt.nthreads; i++)
        if (opt.threads[i] > m)
            m = opt.threads[i];
    return m;
}

static int has_j(uint64_t j)
{
    for (int i = 0; i < opt.nj; i++)
        if (opt.js[i] == j)
            return 1;
    return 0;
}

/* Main j for the per-design tables: 11 if measured, else the first. */
static uint64_t main_j(void) { return has_j(11) ? 11 : opt.js[0]; }

static void header_variants(FILE *f, const char *first)
{
    fprintf(f, "| %s |", first);
    for (int i = 0; i < opt.nvariants; i++)
        fprintf(f, " %s |", vnames[opt.variants[i]]);
    fprintf(f, "\n|---|");
    for (int i = 0; i < opt.nvariants; i++)
        fprintf(f, "---:|");
    fprintf(f, "\n");
}

static void print_metric_table(FILE *f, uint64_t j, int d, int m, int which)
{
    static const char *const titles[] = {"Throughput (M ops/s, all threads)", "p50 latency (ns)",
                                         "p99 latency (ns)", "p99.9 latency (ns)"};
    fprintf(f, "\n**%s**\n\n", titles[which]);
    header_variants(f, "threads");
    for (int ti = 0; ti < opt.nthreads; ti++) {
        fprintf(f, "| %llu |", (unsigned long long)opt.threads[ti]);
        for (int vi = 0; vi < opt.nvariants; vi++) {
            const Result *r = find(opt.variants[vi], j, opt.threads[ti], d, m);
            if (r == NULL) {
                fprintf(f, " - |");
                continue;
            }
            const double v = which == 0 ? r->mops : which == 1 ? r->p50 : which == 2 ? r->p99 : r->p999;
            if (which == 0)
                fprintf(f, " %.1f |", v);
            else
                fprintf(f, " %.0f |", v);
        }
        fprintf(f, "\n");
    }
}

static void print_report(void)
{
    FILE *f = stdout;
    const uint64_t tmax = max_threads();
    const uint64_t jm = main_j();

    fprintf(f, "# VDC memtable multithreaded benchmark\n\n## System\n\n");
    bench_print_system_info(f);

    fprintf(f, "\n## Configuration\n\n| Setting | Value |\n|---|---|\n| Variants |");
    for (int i = 0; i < opt.nvariants; i++)
        fprintf(f, "%s %s", i ? "," : "", vnames[opt.variants[i]]);
    fprintf(f, " |\n| Threads (total, consumers included) |");
    for (int i = 0; i < opt.nthreads; i++)
        fprintf(f, "%s %llu", i ? "," : "", (unsigned long long)opt.threads[i]);
    fprintf(f, " |\n| Consumers (sharded / mpmc) | %s |\n",
            opt.consumers ? "fixed by --consumers" : "half of the threads; mpsc always 1");
    fprintf(f, "| j values |");
    for (int i = 0; i < opt.nj; i++)
        fprintf(f, "%s %llu", i ? "," : "", (unsigned long long)opt.js[i]);
    fprintf(f, " |\n| Keys | %llu |\n| Operations per worker / producer | %llu |\n| Repetitions | %llu (median "
               "throughput; latency percentiles over all samples) |\n",
            (unsigned long long)opt.keys, (unsigned long long)opt.ops, (unsigned long long)opt.reps);
    fprintf(f, "| Async window | %llu requests per producer |\n| Queue capacity | %llu |\n| Latency sampling | 1 of "
               "every %llu operations |\n| Pages | %s |\n| CPU pinning | %s |\n\n",
            (unsigned long long)opt.window, (unsigned long long)opt.queue_cap, (unsigned long long)opt.sample,
            opt.page_mode ? "hugetlb" : "base", opt.ncpus ? "--cpus list" : "thread i on CPU i (Linux only)");
    fprintf(f, "Workloads:");
    for (int i = 0; i < opt.nmixes; i++)
        fprintf(f, " **%s** = %s;", mixnames[opt.mixes[i]], mixdesc[opt.mixes[i]]);
    fprintf(f, "\nDistributions: **uniform**; **zipf** (theta 0.99, hottest keys 0, 1, 2, ...); **interleaved** "
               "(worker w uses keys w, w+W, w+2W, ...: neighbouring keys at the same time).\n"
               "Delegation designs spend part of the thread budget on consumers: their throughput is what the "
               "producers get done with the same number of cores.\n");

    /* Checks */
    fprintf(f, "\n## Checks\n\n| Variant | torn / misrouted gets | wrong statuses | load left every key present |\n"
               "|---|---:|---:|---|\n");
    int all_ok = 1;
    for (int vi = 0; vi < opt.nvariants; vi++) {
        const int v = opt.variants[vi];
        uint64_t bv = 0, bs = 0;
        int cnt_ok = -1;
        for (size_t i = 0; i < nresults; i++)
            if (results[i].variant == v) {
                bv += results[i].bad_value;
                bs += results[i].bad_status;
                if (results[i].count_ok != -1)
                    cnt_ok = (cnt_ok == -1) ? results[i].count_ok : (cnt_ok && results[i].count_ok);
            }
        all_ok &= (bv == 0 && bs == 0 && cnt_ok != 0);
        fprintf(f, "| %s | %llu %s | %llu %s | %s |\n", vnames[v], (unsigned long long)bv, bv ? "❌" : "✅",
                (unsigned long long)bs, bs ? "❌" : "✅",
                cnt_ok == -1 ? "not run" : cnt_ok ? "✅ yes" : "❌ NO");
    }
    fprintf(f, "\nOverall: %s\n", all_ok ? "✅ every get returned a whole, correct value" : "❌ see the rows above");

    /* Best design at the largest thread count */
    fprintf(f, "\n## Fastest design at %llu threads (j = %llu, M ops/s)\n\n| workload |", (unsigned long long)tmax,
            (unsigned long long)jm);
    for (int di = 0; di < opt.ndists; di++)
        fprintf(f, " %s |", distnames[opt.dists[di]]);
    fprintf(f, "\n|---|");
    for (int di = 0; di < opt.ndists; di++)
        fprintf(f, "---|");
    fprintf(f, "\n");
    for (int mi = 0; mi < opt.nmixes; mi++) {
        fprintf(f, "| %s |", mixnames[opt.mixes[mi]]);
        for (int di = 0; di < opt.ndists; di++) {
            const Result *best = NULL;
            for (int vi = 0; vi < opt.nvariants; vi++) {
                const Result *r = find(opt.variants[vi], jm, tmax, opt.dists[di], opt.mixes[mi]);
                if (r != NULL && (best == NULL || r->mops > best->mops))
                    best = r;
            }
            if (best != NULL)
                fprintf(f, " %s (%.1f) |", vnames[best->variant], best->mops);
            else
                fprintf(f, " - |");
        }
        fprintf(f, "\n");
    }

    /* Scaling */
    const uint64_t tmin = opt.threads[0];
    fprintf(f, "\n## Scaling: throughput at %llu threads / throughput at the smallest thread count (j = %llu)\n\n"
               "Direct designs are divided by their %llu-thread result; delegation designs by their smallest run "
               "(at least 2 threads: one consumer, one producer).\n\n",
            (unsigned long long)tmax, (unsigned long long)jm, (unsigned long long)tmin);
    header_variants(f, "workload / dist");
    for (int mi = 0; mi < opt.nmixes; mi++)
        for (int di = 0; di < opt.ndists; di++) {
            fprintf(f, "| %s / %s |", mixnames[opt.mixes[mi]], distnames[opt.dists[di]]);
            for (int vi = 0; vi < opt.nvariants; vi++) {
                const int v = opt.variants[vi];
                const Result *hi = find(v, jm, tmax, opt.dists[di], opt.mixes[mi]);
                const Result *lo = NULL;
                for (int ti = 0; ti < opt.nthreads && lo == NULL; ti++)
                    lo = find(v, jm, opt.threads[ti], opt.dists[di], opt.mixes[mi]);
                if (hi != NULL && lo != NULL && lo->mops > 0 && hi != lo)
                    fprintf(f, " %.1fx |", hi->mops / lo->mops);
                else
                    fprintf(f, " - |");
            }
            fprintf(f, "\n");
        }

    /* VDC scatter vs identity */
    if (opt.nj > 1 && has_j(20)) {
        fprintf(f, "\n## VDC scatter vs identity (j = 20) at %llu threads\n\nCell = M ops/s for each j, and the "
                   "best VDC j relative to identity. Above 1.00x = the permutation helps.\n\n",
                (unsigned long long)tmax);
        header_variants(f, "workload / dist");
        for (int mi = 0; mi < opt.nmixes; mi++)
            for (int di = 0; di < opt.ndists; di++) {
                fprintf(f, "| %s / %s |", mixnames[opt.mixes[mi]], distnames[opt.dists[di]]);
                for (int vi = 0; vi < opt.nvariants; vi++) {
                    const int v = opt.variants[vi];
                    const Result *id = find(v, 20, tmax, opt.dists[di], opt.mixes[mi]);
                    double best = 0.0;
                    char buf[160];
                    size_t pos = 0;
                    buf[0] = '\0';
                    for (int ji = 0; ji < opt.nj; ji++) {
                        const Result *r = find(v, opt.js[ji], tmax, opt.dists[di], opt.mixes[mi]);
                        if (r == NULL)
                            continue;
                        if (pos < sizeof(buf))
                            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%s%.1f", pos ? " / " : "", r->mops);
                        if (opt.js[ji] != 20 && r->mops > best)
                            best = r->mops;
                    }
                    if (id != NULL && id->mops > 0 && best > 0)
                        fprintf(f, " %s (%.2fx) |", buf, best / id->mops);
                    else
                        fprintf(f, " - |");
                }
                fprintf(f, "\n");
            }
        fprintf(f, "\nj order in each cell:");
        for (int ji = 0; ji < opt.nj; ji++)
            fprintf(f, "%s j=%llu", ji ? " /" : "", (unsigned long long)opt.js[ji]);
        fprintf(f, ".\n");
    }

    /* Per workload tables */
    fprintf(f, "\n## Results by workload (j = %llu)\n", (unsigned long long)jm);
    for (int mi = 0; mi < opt.nmixes; mi++)
        for (int di = 0; di < opt.ndists; di++) {
            fprintf(f, "\n### %s / %s\n", mixnames[opt.mixes[mi]], distnames[opt.dists[di]]);
            for (int w = 0; w < 4; w++)
                print_metric_table(f, jm, opt.dists[di], opt.mixes[mi], w);
        }
    if (opt.nj > 1) {
        fprintf(f, "\n## Throughput for the other j values\n");
        for (int ji = 0; ji < opt.nj; ji++) {
            if (opt.js[ji] == jm)
                continue;
            for (int mi = 0; mi < opt.nmixes; mi++)
                for (int di = 0; di < opt.ndists; di++) {
                    fprintf(f, "\n### j = %llu, %s / %s\n", (unsigned long long)opt.js[ji], mixnames[opt.mixes[mi]],
                            distnames[opt.dists[di]]);
                    print_metric_table(f, opt.js[ji], opt.dists[di], opt.mixes[mi], 0);
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
        fprintf(f, "variant,j,threads,consumers,producers,dist,mix,mops,p50_ns,p99_ns,p999_ns,bad_value,"
                   "bad_status,count_ok,forwarded\n");
    for (size_t i = 0; i < nresults; i++) {
        const Result *r = &results[i];
        fprintf(f, "%s,%llu,%llu,%d,%d,%s,%s,%.3f,%.0f,%.0f,%.0f,%llu,%llu,%d,%llu\n", vnames[r->variant],
                (unsigned long long)r->j, (unsigned long long)r->threads, r->consumers, r->producers,
                distnames[r->dist], mixnames[r->mix], r->mops, r->p50, r->p99, r->p999,
                (unsigned long long)r->bad_value, (unsigned long long)r->bad_status, r->count_ok,
                (unsigned long long)r->forwarded);
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

    if (opt.page_mode == VDC_PAGES_HUGETLB) {
        void *p = vdc_map(VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB, 1);
        if (p == NULL) {
            fprintf(stderr, "ERROR: cannot map a 2 MiB hugetlb page (%s). Run: sudo scripts/setup_hugepages.sh\n",
                    strerror(errno));
            return 2;
        }
        vdc_unmap(p, VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB);
    }

    /* Shared workload inputs. */
    g_perm = malloc(opt.keys * sizeof(uint32_t));
    if (g_perm == NULL)
        return 2;
    for (uint64_t i = 0; i < opt.keys; i++)
        g_perm[i] = (uint32_t)i;
    BenchRng pr = {opt.seed};
    bench_shuffle_u32(g_perm, opt.keys, &pr);
    zipf_init(&g_zipf, opt.keys, 0.99);

    int core_ids[MAX_THREADS];
    for (int vi = 0; vi < opt.nvariants; vi++) {
        const int v = opt.variants[vi];
        for (int ji = 0; ji < opt.nj; ji++) {
            const uint64_t j = opt.js[ji];
            CMemtable cm;
            int have_cm = 0;
            if (is_direct(v)) {
                if (cm_init(&cm, (CmMode)v, (unsigned)j, (VdcPageMode)opt.page_mode, 1) != MT_OK) {
                    fprintf(stderr, "ERROR: cannot create the %s memtable (%s)\n", vnames[v], strerror(errno));
                    return 2;
                }
                have_cm = 1;
            }
            for (int ti = 0; ti < opt.nthreads; ti++) {
                const uint64_t T = opt.threads[ti];
                VdcService *sv = NULL;
                int consumers = 0;
                if (!is_direct(v)) {
                    if (T < 2)
                        continue; /* needs at least one producer and one consumer */
                    const SvMode mode = sv_mode_of(v);
                    consumers = mode == SV_MPSC ? 1
                                : opt.consumers ? (int)(opt.consumers < T ? opt.consumers : T - 1)
                                                : (int)(T / 2 > 0 ? T / 2 : 1);
                    for (int c = 0; c < consumers; c++)
                        core_ids[c] = cpu_of(c);
                    SvConfig cfg = {mode, (unsigned)consumers, (unsigned)j, (VdcPageMode)opt.page_mode,
                                    (size_t)opt.queue_cap, core_ids};
                    sv = sv_create(&cfg);
                    if (sv == NULL) {
                        fprintf(stderr, "ERROR: cannot create the %s service (%s)\n", vnames[v], strerror(errno));
                        return 2;
                    }
                }
                for (int di = 0; di < opt.ndists; di++) {
                    const int d = opt.dists[di];
                    int loaded = 0;
                    for (int mi = 0; mi < opt.nmixes; mi++) {
                        const int m = opt.mixes[mi];
                        if (m != MIX_LOAD && !loaded) {
                            clear_table(have_cm ? &cm : NULL, sv);
                            preload(v, have_cm ? &cm : NULL, sv);
                        }
                        const uint64_t t0 = bench_now_ns();
                        run_cell(v, j, T, d, m, have_cm ? &cm : NULL, sv, consumers);
                        loaded = 1; /* after a load or a mix the table holds every key */
                        const Result *r = &results[nresults - 1];
                        fprintf(stderr, "[%s j=%llu T=%llu] %-11s %-4s %7.1f Mops/s  p99 %6.0f ns  (%.1f s)\n",
                                vnames[v], (unsigned long long)j, (unsigned long long)T, distnames[d], mixnames[m],
                                r->mops, r->p99, (double)(bench_now_ns() - t0) / 1e9);
                    }
                }
                if (sv != NULL)
                    sv_destroy(sv);
            }
            if (have_cm)
                cm_destroy(&cm);
        }
    }

    print_report();
    if (opt.csv != NULL)
        write_csv();

    int failed = 0;
    for (size_t i = 0; i < nresults; i++)
        failed |= results[i].bad_value || results[i].bad_status || results[i].count_ok == 0;
    if (failed)
        fprintf(stderr, "ERROR: wrong results detected (see the Checks table)\n");
    free(g_perm);
    free(results);
    return failed ? 1 : 0;
}
