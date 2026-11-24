/*
 * dat_mvscale - experiment 7: per-key versions and range scans, with threads,
 * at 1M - 100M keys.
 *
 * Structures (one exists at a time):
 *   dat-flat, dat-rows   DatConc, seqlock, unversioned (experiment 6's best design, + dc_scan)
 *   mv-flat,  mv-rows    DatMV: same nodes + per-key version chains + snapshot reads and scans
 *   skiplist             single-threaded skip list (the usual memtable): 1 thread only, small N only
 * rows = the two-level layout with 2 MiB rows (32,768 keys per row).
 *
 * Workloads (keys dense 0 .. N-1; uniform or zipf keys / scan starts):
 *   load   T threads insert every key (random order) into an empty table
 *   A      50% get / 50% update
 *   C      100% get
 *   E      95% scan of 100 keys / 5% update (YCSB E with a fixed scan length)
 *   S10 / S100 / S1000   100% scans of 10 / 100 / 1,000 keys
 * Gets and scans read the newest version. Every write takes a sequence number
 * from one global counter, 64 at a time per thread, so the versioned table sees
 * late (out-of-order) writes, as in a real LSM.
 *
 * Snapshot-depth sweep (mv only, at --depth-keys, --depth-threads): every key
 * gets version seq 1; the first 2^20 keys get versions 2 .. d (d = 1, 2, 4, 8,
 * 16); gets and 100-key scans run at the newest version and at snapshot 1 (the
 * oldest: walks d-1 older versions).
 *
 * Checks: every get returns a value holding its key; every scan returns
 * exactly the expected number of keys, ascending, with valid values (values
 * checked on 1 of every 16 scans); after a load every key is present; after
 * every write workload every version chain is sorted by seqno; the depth sweep
 * checks that the snapshot read returned the version of the snapshot.
 *
 * Old versions are dropped (dv_drop_history, untimed) before every repetition
 * of a write workload, so each repetition starts from the same state.
 *
 * Memory rules (as experiments 5-6): the memory of every (structure, N) is
 * computed first; runs over the budget (default 9 GiB, at most 10) are
 * skipped; real memory is measured after creation; above 10 GiB the program stops.
 */
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baselines.h"
#include "bench_common.h"
#include "dat/dat_conc.h"
#include "dat/dat_keys.h"
#include "dat/dat_mv.h"

#define MAX_LIST 16
#define MAX_THREADS 256
#define M (1000000ull)
#define GIB ((uint64_t)1 << 30)
#define MIB ((uint64_t)1 << 20)
#define OVERHEAD (256 * MIB)
#define HARD_LIMIT ((uint64_t)10 * GIB)
#define ROW_BITS 15u
#define MAX_SCAN 1000u
#define DEPTH_KEYS ((uint64_t)1 << 20)
#define NDEPTHS 5
static const uint64_t depths[NDEPTHS] = {1, 2, 4, 8, 16};

enum { S_DAT_FLAT, S_DAT_ROWS, S_MV_FLAT, S_MV_ROWS, S_SKIPLIST, S_COUNT };
static const char *const st_names[S_COUNT] = {"dat-flat", "dat-rows", "mv-flat", "mv-rows", "skiplist"};
static int is_mv(int s) { return s == S_MV_FLAT || s == S_MV_ROWS; }
static int is_dat(int s) { return s == S_DAT_FLAT || s == S_DAT_ROWS; }
static int is_rows(int s) { return s == S_DAT_ROWS || s == S_MV_ROWS; }

enum { MIX_LOAD, MIX_A, MIX_C, MIX_E, MIX_S10, MIX_S100, MIX_S1000, MIX_COUNT, MIX_DGET = MIX_COUNT, MIX_DSCAN };
static const char *const mix_names[MIX_COUNT + 2] = {"load", "A", "C", "E", "S10", "S100", "S1000", "depth-get",
                                                     "depth-scan100"};
static const char *const mix_desc[MIX_COUNT] = {
    "insert every key into an empty table", "50% get / 50% update", "100% get",
    "95% scan of 100 keys / 5% update",     "100% scans of 10 keys", "100% scans of 100 keys",
    "100% scans of 1,000 keys"};
static const int upd_pct[MIX_COUNT] = {0, 50, 0, 5, 0, 0, 0};
static const int scan_pct[MIX_COUNT] = {0, 0, 0, 95, 100, 100, 100};
static const uint32_t scan_len[MIX_COUNT + 2] = {0, 0, 0, 100, 10, 100, 1000, 0, 100};
static int scan_only(int mix) { return mix == MIX_S10 || mix == MIX_S100 || mix == MIX_S1000 || mix == MIX_DSCAN; }

enum { D_UNIFORM, D_ZIPF, D_COUNT };
static const char *const dist_names[D_COUNT] = {"uniform", "zipf"};
static const char *const page_names[2] = {"base", "hugetlb"};

enum { OP_GET, OP_UPD, OP_SCAN };

typedef struct Options {
    int structs[S_COUNT];
    int nstructs;
    uint64_t keys[MAX_LIST];
    int nkeys;
    uint64_t threads[MAX_LIST];
    int nthreads;
    int mixes[MIX_COUNT];
    int nmixes;
    int dists[D_COUNT];
    int ndists;
    uint64_t ops, reps, sample, seed, sl_max, depth_keys, depth_threads;
    double budget_gib;
    int page_mode;
    uint64_t cpus[MAX_THREADS];
    int ncpus;
    const char *csv;
    int plan_only;
} Options;

typedef struct Result {
    int st, mix, dist; /* st = -1: plan row */
    uint64_t n, threads, depth;
    int old; /* depth sweep: read at the oldest snapshot */
    double mops, mkeys, p50, p99, p999;
    uint64_t bad_value, bad_status, bad_scan, chains_bad;
    int count_ok; /* -1 not checked */
    int ran;
    uint64_t versions, mem_plan, mem_measured;
    char reason[128];
} Result;

static Options opt;
static Result *res;
static size_t nres, capres;
static uint64_t budget, peak_measured;

/* ---------------------------------------------------------------- helpers */

static void die_oom(void)
{
    fprintf(stderr, "error: out of memory\n");
    exit(2);
}

static void add_res(const Result *r)
{
    if (nres == capres) {
        capres = capres ? capres * 2 : 512;
        Result *p = realloc(res, capres * sizeof(*res));
        if (p == NULL)
            die_oom();
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

static void check_memory(void)
{
    const uint64_t now = measured_bytes();
    peak_measured = now > peak_measured ? now : peak_measured;
    if (now > HARD_LIMIT) {
        fprintf(stderr, "FATAL: process memory %.2f GiB exceeds the 10 GiB hard limit; stopping\n", gib(now));
        exit(3);
    }
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

static int has_thread(uint64_t T)
{
    for (int i = 0; i < opt.nthreads; i++)
        if (opt.threads[i] == T)
            return 1;
    return 0;
}

/* Operations per thread for a mix (scans: fewer, so each workload reads a similar number of keys). */
static uint64_t ops_for(int mix)
{
    if (mix == MIX_E || mix == MIX_DSCAN) {
        const uint64_t o = opt.ops / 10;
        return o < 1000 ? 1000 : o;
    }
    if (mix == MIX_S10 || mix == MIX_S100 || mix == MIX_S1000) {
        uint64_t o = opt.ops * 10 / scan_len[mix];
        o = o > opt.ops ? opt.ops : o;
        return o < 1000 ? 1000 : o;
    }
    return opt.ops;
}

static void usage(FILE *f)
{
    fprintf(f, "Usage: dat_mvscale [options]   (experiment 7: versions and range scans)\n"
               "  --structs LIST   dat-flat,dat-rows,mv-flat,mv-rows,skiplist   (default all)\n"
               "  --keys LIST      key counts, max 134217728                   (default 1M,10M,100M)\n"
               "  --threads LIST                                               (default 1,2,4,6,12)\n"
               "  --mixes LIST     load,A,C,E,S10,S100,S1000                   (default all)\n"
               "  --dists LIST     uniform,zipf                                (default uniform,zipf)\n"
               "  --ops N          operations per thread (gets/updates; scans: N*10/length)  (default 500K)\n"
               "  --reps N         repetitions, median throughput              (default 3)\n"
               "  --sample N       time 1 of every N operations                (default 64)\n"
               "  --sl-max-keys N  skip list only up to N keys (1 thread)      (default 10M)\n"
               "  --depth-keys N   key count of the snapshot-depth sweep, 0 = none (default 10M)\n"
               "  --depth-threads N  threads of the sweep                      (default: largest of --threads)\n"
               "  --pages base|hugetlb                                         (default hugetlb)\n"
               "  --cpus LIST      CPU of thread 0, 1, ...                     (default 0,1,2,...)\n"
               "  --budget-gib X   memory budget (default 9, at most 10)\n"
               "  --csv FILE       append results as CSV\n"
               "  --seed N         (default 42)\n"
               "  --plan           print the memory plan and exit (stderr: HUGEPAGES_NEEDED=n)\n");
}

static int parse_args(int argc, char **argv)
{
    memset(&opt, 0, sizeof(opt));
    for (int s = 0; s < S_COUNT; s++)
        opt.structs[s] = s;
    opt.nstructs = S_COUNT;
    const uint64_t kk[] = {1 * M, 10 * M, 100 * M};
    memcpy(opt.keys, kk, sizeof(kk));
    opt.nkeys = 3;
    const uint64_t tt[] = {1, 2, 4, 6, 12};
    memcpy(opt.threads, tt, sizeof(tt));
    opt.nthreads = 5;
    for (int m = 0; m < MIX_COUNT; m++)
        opt.mixes[m] = m;
    opt.nmixes = MIX_COUNT;
    opt.dists[0] = D_UNIFORM;
    opt.dists[1] = D_ZIPF;
    opt.ndists = 2;
    opt.ops = 500000;
    opt.reps = 3;
    opt.sample = 64;
    opt.seed = 42;
    opt.sl_max = 10 * M;
    opt.depth_keys = 10 * M;
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
        if (!strcmp(a, "--structs"))
            n = opt.nstructs = bench_parse_name_list(v, st_names, S_COUNT, opt.structs, S_COUNT);
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
        else if (!strcmp(a, "--sl-max-keys"))
            n = bench_parse_u64_list(v, &opt.sl_max, 1);
        else if (!strcmp(a, "--depth-keys")) {
            if (!strcmp(v, "0")) {
                opt.depth_keys = 0;
                n = 1;
            } else
                n = bench_parse_u64_list(v, &opt.depth_keys, 1);
        } else if (!strcmp(a, "--depth-threads"))
            n = bench_parse_u64_list(v, &opt.depth_threads, 1);
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
        if (opt.keys[i] < 2 * MAX_SCAN || opt.keys[i] > DAT_DOMAIN) {
            fprintf(stderr, "error: key counts must be %u .. %llu\n", 2 * MAX_SCAN, (unsigned long long)DAT_DOMAIN);
            return -1;
        }
    for (int i = 0; i < opt.nthreads; i++)
        if (opt.threads[i] == 0 || opt.threads[i] > MAX_THREADS) {
            fprintf(stderr, "error: threads must be 1 .. %d\n", MAX_THREADS);
            return -1;
        }
    if (opt.depth_threads == 0)
        opt.depth_threads = max_threads();
    return 0;
}

/* ---------------------------------------------------------------- memory plan */

static uint64_t round_up(uint64_t x, uint64_t u) { return (x + u - 1) / u * u; }
static uint64_t unit_bytes(void)
{
    return opt.page_mode == VDC_PAGES_HUGETLB ? VDC_HUGE_PAGE_SIZE : vdc_page_size();
}

static int is_depth_cell(int s, uint64_t n) { return is_mv(s) && opt.depth_keys != 0 && n == opt.depth_keys; }
static uint64_t depth_hot(uint64_t n)
{
    uint64_t h = DEPTH_KEYS;
    while (h > n / 2)
        h >>= 1;
    return h;
}

/* Version records the mv table needs for N (one repetition of the biggest write workload, or the sweep). */
static uint64_t version_records(uint64_t n)
{
    const uint64_t T = max_threads();
    uint64_t per = 0;
    for (int i = 0; i < opt.nmixes; i++) {
        const int mx = opt.mixes[i];
        const uint64_t u = ops_for(mx) * (uint64_t)upd_pct[mx] / 100;
        const uint64_t bound = u + u / 50 + 1000; /* random draws: +2% + 1,000 */
        per = bound > per ? bound : per;
    }
    uint64_t v = T * per + T * 2 * DV_CHUNK + 4096;
    if (opt.depth_keys != 0 && n == opt.depth_keys) {
        const uint64_t d = depth_hot(n) * (depths[NDEPTHS - 1] - 1) + 4 * DV_CHUNK + 4096;
        v = d > v ? d : v;
    }
    return v;
}

static uint64_t node_bytes(int s, uint64_t n)
{
    const uint64_t unit = unit_bytes();
    if (!is_rows(s)) {
        const uint64_t per = VDC_HUGE_PAGE_SIZE / 64;
        return round_up(round_up(n, per) * 64, unit);
    }
    const uint64_t rows = (n + ((uint64_t)1 << ROW_BITS) - 1) >> ROW_BITS;
    return round_up((DAT_DOMAIN >> ROW_BITS) * 4, unit) + round_up(rows * (64ull << ROW_BITS), unit);
}

static uint64_t sl_arena_bytes(uint64_t n) { return n * 80 + 16 * MIB; }

/* Bytes in mapped pages (hugetlb-eligible). */
static uint64_t mapped_bytes(int s, uint64_t n)
{
    if (s == S_SKIPLIST)
        return round_up(sl_arena_bytes(n), unit_bytes());
    uint64_t b = node_bytes(s, n);
    if (is_mv(s))
        b += round_up(version_records(n) * 64, unit_bytes());
    return b;
}

static uint64_t structure_bytes(int s, uint64_t n)
{
    return mapped_bytes(s, n) + (is_dat(s) ? (uint64_t)DC_STRIPES * 64 : 0);
}

static uint64_t worklist_bytes(int s, uint64_t n)
{
    uint64_t per = 0;
    for (int i = 0; i < opt.nmixes; i++)
        if (opt.mixes[i] != MIX_LOAD)
            per += ops_for(opt.mixes[i]) * 5 * (uint64_t)opt.ndists;
    uint64_t b = max_threads() * per;
    if (is_depth_cell(s, n))
        b += opt.depth_threads * (ops_for(MIX_DGET) + ops_for(MIX_DSCAN)) * 5;
    return b;
}

static uint64_t plan_total(int s, uint64_t n)
{
    return structure_bytes(s, n) + n * 4 + worklist_bytes(s, n) + max_threads() * 64 * 1024 + OVERHEAD;
}

static int runs_struct(int s, uint64_t n, uint64_t T)
{
    return s != S_SKIPLIST || (T == 1 && n <= opt.sl_max);
}

/* ---------------------------------------------------------------- stores */

typedef struct Store {
    int s;
    DatConc dc;
    DatMV mv;
    SkipList sl;
} Store;

static int st_init(Store *st, int s, uint64_t n)
{
    memset(st, 0, sizeof(*st));
    st->s = s;
    const VdcPageMode pm = (VdcPageMode)opt.page_mode;
    if (is_dat(s))
        return dc_init(&st->dc, is_rows(s) ? DC_ROWS : DC_FLAT, DC_SEQLOCK, ROW_BITS, n, 0, pm);
    if (is_mv(s))
        return dv_init(&st->mv, is_rows(s) ? DC_ROWS : DC_FLAT, ROW_BITS, n, 0, version_records(n), pm);
    return sl_init(&st->sl, sl_arena_bytes(n), pm, 1);
}

static void st_destroy(Store *st)
{
    if (is_dat(st->s))
        dc_destroy(&st->dc);
    else if (is_mv(st->s))
        dv_destroy(&st->mv);
    else
        sl_destroy(&st->sl);
}

static void st_clear(Store *st)
{
    if (is_dat(st->s))
        dc_clear(&st->dc);
    else if (is_mv(st->s))
        dv_clear(&st->mv);
    else
        sl_clear(&st->sl);
}

static uint64_t st_count(Store *st, uint64_t n)
{
    if (is_dat(st->s))
        return dc_count(&st->dc);
    if (is_mv(st->s))
        return dv_count(&st->mv);
    (void)n;
    return st->sl.num_entries;
}

static inline int st_put(Store *st, uint32_t key, const uint64_t *w, uint64_t seq)
{
    switch (st->s) {
    case S_DAT_FLAT:
    case S_DAT_ROWS:
        return dc_put(&st->dc, key, w, 32, seq);
    case S_MV_FLAT:
    case S_MV_ROWS:
        return dv_put(&st->mv, key, w, 32, seq);
    default:
        return sl_put(&st->sl, key, w, 32);
    }
}

static inline int st_get(Store *st, uint32_t key, uint64_t snap, uint64_t *w, uint32_t *len)
{
    switch (st->s) {
    case S_DAT_FLAT:
    case S_DAT_ROWS:
        return dc_get(&st->dc, key, w, len, NULL);
    case S_MV_FLAT:
    case S_MV_ROWS:
        return dv_get(&st->mv, key, snap, w, len, NULL);
    default: {
        const void *v = NULL;
        const int rc = sl_get(&st->sl, key, &v, len);
        if (rc == MT_OK)
            memcpy(w, v, 32);
        return rc;
    }
    }
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
    uint8_t *kind;
    size_t n;
} Work;

#define MAX_COMBOS 32
static Work *work_lists; /* [combo * T + t] */
static int ncombos;
static int combo_dist[MAX_COMBOS], combo_mix[MAX_COMBOS];

static void free_work(uint64_t T)
{
    if (work_lists == NULL)
        return;
    for (uint64_t i = 0; i < (uint64_t)ncombos * T; i++) {
        free(work_lists[i].keys);
        free(work_lists[i].kind);
    }
    free(work_lists);
    work_lists = NULL;
    ncombos = 0;
}

static void fill_work(Work *w, int mix, int dist, uint64_t n, uint64_t key_range, const Zipf *z, uint64_t seed)
{
    w->n = ops_for(mix);
    w->keys = malloc(w->n * sizeof(uint32_t));
    w->kind = malloc(w->n);
    if (w->keys == NULL || w->kind == NULL)
        die_oom();
    BenchRng r = {seed};
    for (size_t i = 0; i < w->n; i++) {
        uint32_t key = dist == D_ZIPF ? zipf_next(z, &r) : (uint32_t)bench_rng_below(&r, key_range);
        uint8_t kind;
        if (mix == MIX_DGET)
            kind = OP_GET;
        else if (mix == MIX_DSCAN)
            kind = OP_SCAN;
        else {
            const uint64_t p = bench_rng_below(&r, 100);
            kind = p < (uint64_t)scan_pct[mix] ? OP_SCAN : p < (uint64_t)(scan_pct[mix] + upd_pct[mix]) ? OP_UPD : OP_GET;
        }
        (void)n;
        w->keys[i] = key;
        w->kind[i] = kind;
    }
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
    if (work_lists == NULL)
        die_oom();
    for (int c = 0; c < ncombos; c++)
        for (uint64_t t = 0; t < T; t++)
            fill_work(&work_lists[(uint64_t)c * T + t], combo_mix[c], combo_dist[c], n, n, z,
                      opt.seed * 1000003ull + n + (uint64_t)combo_mix[c] * 7919u + (uint64_t)combo_dist[c] * 31u +
                          t * 104729u);
}

/* ---------------------------------------------------------------- threads */

static uint64_t g_seq = (uint64_t)1 << 20; /* write sequence numbers (above the depth sweep's 1 .. 16) */
static int g_ready, g_go;

typedef struct Ctx {
    Store *st;
    int cpu;
    int load;
    const uint32_t *load_keys;
    uint64_t n, T, id;
    const Work *work;
    uint32_t len;        /* scan length */
    uint64_t snap;       /* snapshot of gets and scans */
    uint64_t expect_lo;  /* depth sweep: low 32 bits of the seqno every read must return (0 = no check) */
    uint64_t hot;        /* depth sweep: keys below this have the deep versions */
    uint64_t expect_hot; /* ... and at the newest snapshot they return this seqno */
    uint64_t seq_next, seq_end;
    uint64_t t_end, ops, keys_read, bad_value, bad_status, bad_scan;
    double *lat;
    size_t nlat;
    DatKV *kv;
    uint32_t *sl_keys;
    const void **sl_vals;
} Ctx;

static inline uint64_t next_seq(Ctx *x)
{
    if (x->seq_next == x->seq_end) {
        x->seq_next = __atomic_fetch_add(&g_seq, 64, __ATOMIC_RELAXED) + 1;
        x->seq_end = x->seq_next + 64;
    }
    return x->seq_next++;
}

static inline void make_value(uint64_t w[4], uint32_t key, uint64_t seq)
{
    const uint64_t x = ((uint64_t)key << 32) | (seq & 0xffffffffu);
    w[0] = w[1] = w[2] = w[3] = x;
}

static inline int value_ok(const Ctx *x, const uint64_t *w, uint32_t key)
{
    if (w[0] != w[1] || w[1] != w[2] || w[2] != w[3] || (uint32_t)(w[0] >> 32) != key)
        return 0;
    if (x->expect_lo) {
        const uint64_t want = (x->snap == DV_LATEST && key < x->hot) ? x->expect_hot : x->expect_lo;
        return (w[0] & 0xffffffffu) == want;
    }
    return 1;
}

/* One scan from start; returns keys read; counts wrong scans. */
static inline uint64_t do_scan(Ctx *x, uint32_t start, int verify)
{
    uint64_t end = (uint64_t)start + x->len;
    const uint64_t expected = (end > x->n ? x->n : end) - start;
    Store *st = x->st;
    size_t got;
    if (st->s == S_SKIPLIST) {
        got = sl_scan(&st->sl, start, (uint32_t)end, x->len, x->sl_keys, x->sl_vals);
        if (verify)
            for (size_t i = 0; i < got; i++) {
                uint64_t w[4];
                memcpy(w, x->sl_vals[i], 32);
                if (x->sl_keys[i] != start + i || !value_ok(x, w, x->sl_keys[i]))
                    x->bad_scan++;
            }
    } else {
        got = is_dat(st->s) ? dc_scan(&st->dc, start, (uint32_t)end, x->len, x->kv, NULL)
                            : dv_scan(&st->mv, start, (uint32_t)end, x->len, x->snap, x->kv, NULL);
        if (verify)
            for (size_t i = 0; i < got; i++) {
                const DatKV *kv = &x->kv[i];
                if (kv->key != start + i || kv->len != 32 ||
                    !value_ok(x, (const uint64_t *)(const void *)kv->value, kv->key))
                    x->bad_scan++;
            }
    }
    if (got != expected)
        x->bad_scan++;
    return got;
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
    Store *st = x->st;
    if (x->load) {
        uint64_t i = 0;
        for (uint64_t idx = x->id; idx < x->n; idx += x->T, i++) {
            const uint32_t key = x->load_keys[idx];
            const int sample = (i % opt.sample) == 0;
            const uint64_t t0 = sample ? bench_now_ns() : 0;
            const uint64_t seq = next_seq(x);
            make_value(w, key, seq);
            if (st_put(st, key, w, seq) != MT_OK)
                x->bad_status++;
            if (sample)
                x->lat[x->nlat++] = (double)(bench_now_ns() - t0);
        }
        x->ops = i;
    } else {
        const Work *wk = x->work;
        uint64_t scans = 0;
        for (size_t i = 0; i < wk->n; i++) {
            const uint32_t key = wk->keys[i];
            const int sample = (i % opt.sample) == 0;
            const uint64_t t0 = sample ? bench_now_ns() : 0;
            switch (wk->kind[i]) {
            case OP_GET: {
                uint32_t len = 0;
                const int rc = st_get(st, key, x->snap, w, &len);
                if (rc != MT_OK)
                    x->bad_status++;
                else if (len != 32 || !value_ok(x, w, key))
                    x->bad_value++;
                x->keys_read++;
                break;
            }
            case OP_UPD: {
                const uint64_t seq = next_seq(x);
                make_value(w, key, seq);
                if (st_put(st, key, w, seq) != MT_OK)
                    x->bad_status++;
                break;
            }
            default:
                x->keys_read += do_scan(x, key, (scans++ & 15) == 0);
                break;
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

typedef struct CellSpec {
    int mix, dist;        /* mix MIX_LOAD = load */
    const Work *work;     /* [T] lists, NULL for load */
    uint64_t snap, depth, expect_lo, hot, expect_hot;
    int old;
} CellSpec;

static void run_cell(Store *st, uint64_t n, uint64_t T, const CellSpec *cs, const uint32_t *load_keys)
{
    const int load = cs->mix == MIX_LOAD;
    const int writes = load ? 0 : upd_pct[cs->mix < MIX_COUNT ? cs->mix : MIX_C] > 0;
    Ctx *ctx = calloc(T, sizeof(Ctx));
    pthread_t *th = calloc(T, sizeof(pthread_t));
    const size_t per_thread_ops = load ? (size_t)((n + T - 1) / T) : cs->work[0].n;
    const size_t lat_per = per_thread_ops / opt.sample + 2;
    double *lat = malloc(lat_per * T * opt.reps * sizeof(double));
    if (ctx == NULL || th == NULL || lat == NULL)
        die_oom();
    for (uint64_t t = 0; t < T; t++) {
        ctx[t].kv = malloc(MAX_SCAN * sizeof(DatKV));
        ctx[t].sl_keys = malloc(MAX_SCAN * sizeof(uint32_t));
        ctx[t].sl_vals = malloc(MAX_SCAN * sizeof(void *));
        if (ctx[t].kv == NULL || ctx[t].sl_keys == NULL || ctx[t].sl_vals == NULL)
            die_oom();
    }
    double mops[20], mkeys[20];
    size_t nlat = 0;
    uint64_t bad_value = 0, bad_status = 0, bad_scan = 0, versions = 0;
    int count_ok = -1;

    for (uint64_t rep = 0; rep < opt.reps; rep++) {
        if (load)
            st_clear(st);
        else if (writes && is_mv(st->s))
            dv_drop_history(&st->mv); /* same starting state every repetition */
        g_ready = 0;
        g_go = 0;
        for (uint64_t t = 0; t < T; t++) {
            Ctx *x = &ctx[t];
            DatKV *kv = x->kv;
            uint32_t *sk = x->sl_keys;
            const void **sv = x->sl_vals;
            memset(x, 0, sizeof(*x));
            x->kv = kv;
            x->sl_keys = sk;
            x->sl_vals = sv;
            x->st = st;
            x->cpu = cpu_of((int)t);
            x->load = load;
            x->load_keys = load_keys;
            x->n = n;
            x->T = T;
            x->id = t;
            x->work = load ? NULL : &cs->work[t];
            x->len = scan_len[cs->mix];
            x->snap = cs->snap;
            x->expect_lo = cs->expect_lo;
            x->hot = cs->hot;
            x->expect_hot = cs->expect_hot;
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
        uint64_t t1 = t0, ops = 0, keys = 0;
        for (uint64_t t = 0; t < T; t++) {
            pthread_join(th[t], NULL);
            t1 = ctx[t].t_end > t1 ? ctx[t].t_end : t1;
            ops += ctx[t].ops;
            keys += ctx[t].keys_read;
            bad_value += ctx[t].bad_value;
            bad_status += ctx[t].bad_status;
            bad_scan += ctx[t].bad_scan;
        }
        const double us = (double)(t1 - t0) / 1e3;
        mops[rep] = t1 > t0 ? (double)ops / us : 0.0;
        mkeys[rep] = t1 > t0 ? (double)keys / us : 0.0;
        size_t pos = nlat;
        for (uint64_t t = 0; t < T; t++) {
            memmove(lat + pos, ctx[t].lat, ctx[t].nlat * sizeof(double));
            pos += ctx[t].nlat;
        }
        nlat = pos;
        if (is_mv(st->s)) {
            const uint64_t v = dv_versions_used(&st->mv);
            versions = v > versions ? v : versions;
        }
        if (load && rep == 0) {
            check_memory();
            count_ok = st_count(st, n) == n;
        }
    }

    Result r;
    memset(&r, 0, sizeof(r));
    if (writes && is_mv(st->s)) {
        r.chains_bad = dv_check(&st->mv, NULL);
    }
    qsort(lat, nlat, sizeof(double), cmp_d);
    r.st = st->s;
    r.mix = cs->mix;
    r.dist = load ? -1 : cs->dist;
    r.n = n;
    r.threads = T;
    r.depth = cs->depth;
    r.old = cs->old;
    r.mops = bench_median(mops, opt.reps);
    r.mkeys = bench_median(mkeys, opt.reps);
    r.p50 = pct(lat, nlat, 0.50);
    r.p99 = pct(lat, nlat, 0.99);
    r.p999 = pct(lat, nlat, 0.999);
    r.bad_value = bad_value;
    r.bad_status = bad_status;
    r.bad_scan = bad_scan;
    r.count_ok = count_ok;
    r.versions = versions;
    r.ran = 1;
    r.mem_plan = plan_total(st->s, n);
    r.mem_measured = measured_bytes();
    add_res(&r);

    char kb[32];
    fmt_keys(kb, sizeof(kb), n);
    char extra[48] = "";
    if (cs->depth)
        snprintf(extra, sizeof(extra), " depth %llu %s", (unsigned long long)cs->depth, cs->old ? "oldest" : "newest");
    fprintf(stderr, "    %-8s %5s T=%-3llu %-7s %-13s%s %8.1f Mops/s %8.1f Mkeys/s  p99 %6.0f ns%s\n",
            st_names[st->s], kb, (unsigned long long)T, load ? "-" : dist_names[cs->dist], mix_names[cs->mix], extra,
            r.mops, r.mkeys, r.p99,
            (bad_value || bad_status || bad_scan || count_ok == 0 || r.chains_bad) ? "  WRONG RESULTS" : "");
    for (uint64_t t = 0; t < T; t++) {
        free(ctx[t].kv);
        free(ctx[t].sl_keys);
        free(ctx[t].sl_vals);
    }
    free(ctx);
    free(th);
    free(lat);
}

/* Snapshot-depth sweep on a loaded mv table: versions 1 .. d on the first `hot` keys. */
static void run_depth_sweep(Store *st, uint64_t n, uint64_t T)
{
    const uint64_t hot = depth_hot(n);
    Work *wg = calloc(T, sizeof(Work)), *ws = calloc(T, sizeof(Work));
    if (wg == NULL || ws == NULL)
        die_oom();
    for (uint64_t t = 0; t < T; t++) {
        fill_work(&wg[t], MIX_DGET, D_UNIFORM, n, hot, NULL, opt.seed * 7777u + t);
        fill_work(&ws[t], MIX_DSCAN, D_UNIFORM, n, hot - scan_len[MIX_DSCAN], NULL, opt.seed * 8888u + t);
    }
    dv_clear(&st->mv);
    uint64_t w[4];
    for (uint64_t k = 0; k < n; k++) {
        make_value(w, (uint32_t)k, 1);
        dv_put(&st->mv, (uint32_t)k, w, 32, 1);
    }
    uint64_t have = 1;
    for (int di = 0; di < NDEPTHS; di++) {
        const uint64_t d = depths[di];
        for (uint64_t j = have + 1; j <= d; j++)
            for (uint64_t k = 0; k < hot; k++) {
                make_value(w, (uint32_t)k, j);
                if (dv_put(&st->mv, (uint32_t)k, w, 32, j) != MT_OK) {
                    fprintf(stderr, "error: depth sweep: version arena full\n");
                    exit(2);
                }
            }
        have = d;
        for (int old = 0; old <= 1; old++)
            for (int sc = 0; sc <= 1; sc++) {
                CellSpec cs = {sc ? MIX_DSCAN : MIX_DGET, D_UNIFORM, sc ? ws : wg, old ? 1 : DV_LATEST, d, 1, hot, d,
                               old};
                run_cell(st, n, T, &cs, NULL);
            }
    }
    for (uint64_t t = 0; t < T; t++) {
        free(wg[t].keys), free(wg[t].kind);
        free(ws[t].keys), free(ws[t].kind);
    }
    free(wg);
    free(ws);
    dv_clear(&st->mv);
}

/* ---------------------------------------------------------------- report */

static const Result *find(int s, uint64_t n, uint64_t T, int dist, int mix)
{
    for (size_t i = 0; i < nres; i++) {
        const Result *r = &res[i];
        if (r->ran && r->st == s && r->n == n && r->threads == T && r->mix == mix && r->depth == 0 &&
            (mix == MIX_LOAD || r->dist == dist))
            return r;
    }
    return NULL;
}

static const Result *find_depth(int s, uint64_t d, int mix, int old)
{
    for (size_t i = 0; i < nres; i++) {
        const Result *r = &res[i];
        if (r->ran && r->st == s && r->depth == d && r->mix == mix && r->old == old)
            return r;
    }
    return NULL;
}

static int nwl;
static int wl_mix[MAX_COMBOS], wl_dist[MAX_COMBOS];

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

/* The number shown for a result: M keys/s for scan-only workloads, M ops/s otherwise. */
static double val(const Result *r) { return scan_only(r->mix) ? r->mkeys : r->mops; }

static void print_val(FILE *f, const Result *r)
{
    if (r)
        fprintf(f, " %.1f |", val(r));
    else
        fprintf(f, " - |");
}

static void table_head(FILE *f, const char *first, const int *cols, int ncols)
{
    fprintf(f, "\n| %s |", first);
    for (int i = 0; i < ncols; i++)
        fprintf(f, " %s |", st_names[cols[i]]);
    fprintf(f, "\n|---|");
    for (int i = 0; i < ncols; i++)
        fprintf(f, "---:|");
    fprintf(f, "\n");
}

static void print_report(void)
{
    FILE *f = stdout;
    const uint64_t tmax = max_threads();
    const uint64_t tmin = opt.threads[0];
    uint64_t nmax = 0;
    for (int i = 0; i < opt.nkeys; i++)
        nmax = opt.keys[i] > nmax ? opt.keys[i] : nmax;
    char kb[32], lb[64];

    fprintf(f, "# Experiment 7: per-key versions and range scans\n\n## System\n\n");
    bench_print_system_info(f);
    fprintf(f, "\n## Configuration\n\n| Setting | Value |\n|---|---|\n| Structures |");
    for (int i = 0; i < opt.nstructs; i++)
        fprintf(f, "%s %s", i ? "," : "", st_names[opt.structs[i]]);
    fprintf(f, " (dat = seqlock, unversioned; mv = versioned; rows = 2 MiB rows; skip list: 1 thread, up to ");
    fmt_keys(kb, sizeof(kb), opt.sl_max);
    fprintf(f, "%s keys) |\n| Key counts |", kb);
    for (int i = 0; i < opt.nkeys; i++) {
        fmt_keys(kb, sizeof(kb), opt.keys[i]);
        fprintf(f, "%s %s", i ? "," : "", kb);
    }
    fprintf(f, " (dense keys 0 .. N-1) |\n| Threads |");
    for (int i = 0; i < opt.nthreads; i++)
        fprintf(f, "%s %llu", i ? "," : "", (unsigned long long)opt.threads[i]);
    fprintf(f, " |\n| Workloads |");
    for (int i = 0; i < opt.nmixes; i++)
        fprintf(f, "%s %s = %s (%llu ops/thread)", i ? ";" : "", mix_names[opt.mixes[i]], mix_desc[opt.mixes[i]],
                opt.mixes[i] == MIX_LOAD ? 0ull : (unsigned long long)ops_for(opt.mixes[i]));
    fprintf(f, " |\n| Key distributions |");
    for (int i = 0; i < opt.ndists; i++)
        fprintf(f, "%s %s", i ? "," : "", dist_names[opt.dists[i]]);
    fprintf(f, " (zipf: theta 0.99, hottest keys 0, 1, 2, ...; for scans: the start key) |\n");
    fprintf(f, "| Repetitions | %llu (median throughput; latency over all samples, 1 in %llu ops timed) |\n",
            (unsigned long long)opt.reps, (unsigned long long)opt.sample);
    fprintf(f, "| Snapshot-depth sweep |");
    if (opt.depth_keys) {
        fmt_keys(kb, sizeof(kb), opt.depth_keys);
        fprintf(f, " %s keys, %llu threads, depths 1, 2, 4, 8, 16 on the first %llu keys |\n", kb,
                (unsigned long long)opt.depth_threads, (unsigned long long)depth_hot(opt.depth_keys));
    } else
        fprintf(f, " off |\n");
    fprintf(f, "| Pages | %s |\n| Memory budget | %.2f GiB; hard stop 10 GiB; peak measured %.2f GiB |\n",
            page_names[opt.page_mode], gib(budget), gib(peak_measured));
    fprintf(f, "| CPU pinning | %s |\n", opt.ncpus ? "--cpus list" : "thread i on CPU i (Linux)");

    fprintf(f, "\n## Memory plan\n\n| keys | structure | memory (structure + version arena + load keys + operation "
               "lists + program) | decision |\n|---|---|---:|---|\n");
    for (size_t i = 0; i < nres; i++)
        if (res[i].st >= 0 && !res[i].ran && res[i].reason[0]) {
            fmt_keys(kb, sizeof(kb), res[i].n);
            fprintf(f, "| %s | %s | %.2f GiB | %s |\n", kb, st_names[res[i].st], gib(res[i].mem_plan), res[i].reason);
        }
    if (opt.plan_only)
        return;

    /* Checks */
    fprintf(f, "\n## Checks\n\n| Structure | wrong gets | wrong statuses | wrong scans | every key present after "
               "load | version chains sorted |\n|---|---:|---:|---:|---|---|\n");
    int all_ok = 1;
    for (int si = 0; si < opt.nstructs; si++) {
        const int s = opt.structs[si];
        uint64_t bv = 0, bs = 0, bc = 0, ch = 0;
        int cnt = -1, any = 0;
        for (size_t i = 0; i < nres; i++)
            if (res[i].ran && res[i].st == s) {
                any = 1;
                bv += res[i].bad_value;
                bs += res[i].bad_status;
                bc += res[i].bad_scan;
                ch += res[i].chains_bad;
                if (res[i].count_ok != -1)
                    cnt = cnt == -1 ? res[i].count_ok : (cnt && res[i].count_ok);
            }
        if (!any)
            continue;
        all_ok &= bv == 0 && bs == 0 && bc == 0 && ch == 0 && cnt != 0;
        fprintf(f, "| %s | %llu %s | %llu %s | %llu %s | %s | %s |\n", st_names[s], (unsigned long long)bv,
                bv ? "❌" : "✅", (unsigned long long)bs, bs ? "❌" : "✅", (unsigned long long)bc, bc ? "❌" : "✅",
                cnt == -1 ? "not run" : cnt ? "✅" : "❌",
                is_mv(s) ? (ch ? "❌" : "✅") : "-");
    }
    fprintf(f, "\nOverall: %s\n", all_ok ? "✅ every operation returned the right result" : "❌ see above");
    fprintf(f, "\nNumbers below: **M keys/s** for the scan-only workloads (S10, S100, S1000), **M ops/s** otherwise "
               "(an E operation is a 100-key scan or an update).\n");

    /* Headline */
    fmt_keys(kb, sizeof(kb), nmax);
    fprintf(f, "\n## All structures at %s keys, %llu threads\n", kb, (unsigned long long)tmax);
    table_head(f, "workload", opt.structs, opt.nstructs);
    for (int w = 0; w < nwl; w++) {
        wl_label(lb, sizeof(lb), w);
        fprintf(f, "| %s |", lb);
        for (int si = 0; si < opt.nstructs; si++)
            print_val(f, find(opt.structs[si], nmax, tmax, wl_dist[w], wl_mix[w]));
        fprintf(f, "\n");
    }

    /* Cost of versions */
    fprintf(f, "\n## Cost of versions: mv / dat (same layout, %s keys)\n\nCell = ratio at %llu thread%s / %llu "
               "threads (1.00 = versions are free).\n\n| workload | flat | rows |\n|---|---:|---:|\n",
            kb, (unsigned long long)tmin, tmin == 1 ? "" : "s", (unsigned long long)tmax);
    for (int w = 0; w < nwl; w++) {
        wl_label(lb, sizeof(lb), w);
        fprintf(f, "| %s |", lb);
        for (int lay = 0; lay < 2; lay++) {
            const int sd = lay ? S_DAT_ROWS : S_DAT_FLAT, sm = lay ? S_MV_ROWS : S_MV_FLAT;
            const uint64_t ts[2] = {tmin, tmax};
            fprintf(f, " ");
            for (int k = 0; k < 2; k++) {
                const Result *a = find(sd, nmax, ts[k], wl_dist[w], wl_mix[w]);
                const Result *b = find(sm, nmax, ts[k], wl_dist[w], wl_mix[w]);
                if (a && b && val(a) > 0)
                    fprintf(f, "%s%.2f", k ? " / " : "", val(b) / val(a));
                else
                    fprintf(f, "%s-", k ? " / " : "");
            }
            fprintf(f, " |");
        }
        fprintf(f, "\n");
    }

    /* Versus the skip list */
    fprintf(f, "\n## Versus the skip list (1 thread)\n\nCell = M keys/s or M ops/s; in brackets: times the skip "
               "list.\n");
    for (int ki = 0; ki < opt.nkeys; ki++) {
        const uint64_t n = opt.keys[ki];
        if (find(S_SKIPLIST, n, 1, -1, MIX_LOAD) == NULL && find(S_SKIPLIST, n, 1, D_UNIFORM, MIX_C) == NULL &&
            find(S_SKIPLIST, n, 1, D_UNIFORM, MIX_S100) == NULL)
            continue;
        fmt_keys(kb, sizeof(kb), n);
        fprintf(f, "\n**%s keys**\n", kb);
        table_head(f, "workload", opt.structs, opt.nstructs);
        for (int w = 0; w < nwl; w++) {
            wl_label(lb, sizeof(lb), w);
            fprintf(f, "| %s |", lb);
            const Result *sl = find(S_SKIPLIST, n, 1, wl_dist[w], wl_mix[w]);
            for (int si = 0; si < opt.nstructs; si++) {
                const Result *r = find(opt.structs[si], n, 1, wl_dist[w], wl_mix[w]);
                if (r && sl && opt.structs[si] != S_SKIPLIST && val(sl) > 0)
                    fprintf(f, " %.1f (%.1fx) |", val(r), val(r) / val(sl));
                else
                    print_val(f, r);
            }
            fprintf(f, "\n");
        }
    }

    /* Throughput vs key count */
    fprintf(f, "\n## Throughput vs key count\n\nCell = %llu thread%s / %llu threads. Does it hold from 1M to 100M "
               "keys?\n",
            (unsigned long long)tmin, tmin == 1 ? "" : "s", (unsigned long long)tmax);
    for (int si = 0; si < opt.nstructs; si++) {
        const int s = opt.structs[si];
        if (s == S_SKIPLIST)
            continue;
        fprintf(f, "\n### %s\n\n| workload |", st_names[s]);
        for (int ki = 0; ki < opt.nkeys; ki++) {
            fmt_keys(kb, sizeof(kb), opt.keys[ki]);
            fprintf(f, " %s |", kb);
        }
        fprintf(f, "\n|---|");
        for (int ki = 0; ki < opt.nkeys; ki++)
            fprintf(f, "---:|");
        fprintf(f, "\n");
        for (int w = 0; w < nwl; w++) {
            wl_label(lb, sizeof(lb), w);
            fprintf(f, "| %s |", lb);
            for (int ki = 0; ki < opt.nkeys; ki++) {
                const Result *a = find(s, opt.keys[ki], tmin, wl_dist[w], wl_mix[w]);
                const Result *b = find(s, opt.keys[ki], tmax, wl_dist[w], wl_mix[w]);
                if (!a && !b)
                    fprintf(f, " skipped |");
                else {
                    fprintf(f, " ");
                    if (a)
                        fprintf(f, "%.1f", val(a));
                    else
                        fprintf(f, "-");
                    if (tmax != tmin) {
                        if (b)
                            fprintf(f, " / %.1f", val(b));
                        else
                            fprintf(f, " / -");
                    }
                    fprintf(f, " |");
                }
            }
            fprintf(f, "\n");
        }
    }

    /* Scaling with threads */
    fmt_keys(kb, sizeof(kb), nmax);
    fprintf(f, "\n## Scaling with threads at %s keys\n", kb);
    for (int si = 0; si < opt.nstructs; si++) {
        const int s = opt.structs[si];
        if (s == S_SKIPLIST)
            continue;
        fprintf(f, "\n### %s\n\n| workload |", st_names[s]);
        for (int ti = 0; ti < opt.nthreads; ti++)
            fprintf(f, " T=%llu |", (unsigned long long)opt.threads[ti]);
        fprintf(f, "\n|---|");
        for (int ti = 0; ti < opt.nthreads; ti++)
            fprintf(f, "---:|");
        fprintf(f, "\n");
        for (int w = 0; w < nwl; w++) {
            wl_label(lb, sizeof(lb), w);
            fprintf(f, "| %s |", lb);
            for (int ti = 0; ti < opt.nthreads; ti++)
                print_val(f, find(s, nmax, opt.threads[ti], wl_dist[w], wl_mix[w]));
            fprintf(f, "\n");
        }
    }

    /* Tail latency */
    fprintf(f, "\n## p99 latency per operation at %s keys, %llu threads (ns; a scan is one operation)\n", kb,
            (unsigned long long)tmax);
    table_head(f, "workload", opt.structs, opt.nstructs);
    for (int w = 0; w < nwl; w++) {
        wl_label(lb, sizeof(lb), w);
        fprintf(f, "| %s |", lb);
        for (int si = 0; si < opt.nstructs; si++) {
            const Result *r = find(opt.structs[si], nmax, tmax, wl_dist[w], wl_mix[w]);
            if (r)
                fprintf(f, " %.0f (p99.9 %.0f) |", r->p99, r->p999);
            else
                fprintf(f, " - |");
        }
        fprintf(f, "\n");
    }

    /* Snapshot depth */
    if (opt.depth_keys) {
        fmt_keys(kb, sizeof(kb), opt.depth_keys);
        fprintf(f, "\n## Snapshot reads vs version depth (%s keys, %llu threads)\n\nThe first %llu keys have d "
                   "versions; \"oldest\" reads at snapshot 1 and walks d-1 older versions. Gets in M ops/s, scans of "
                   "100 keys in M keys/s.\n",
                kb, (unsigned long long)opt.depth_threads, (unsigned long long)depth_hot(opt.depth_keys));
        for (int s = S_MV_FLAT; s <= S_MV_ROWS; s++) {
            if (find_depth(s, 1, MIX_DGET, 0) == NULL)
                continue;
            fprintf(f, "\n### %s\n\n| depth | get newest | get oldest | scan newest | scan oldest |\n"
                       "|---:|---:|---:|---:|---:|\n",
                    st_names[s]);
            for (int di = 0; di < NDEPTHS; di++) {
                fprintf(f, "| %llu |", (unsigned long long)depths[di]);
                for (int sc = 0; sc <= 1; sc++)
                    for (int old = 0; old <= 1; old++)
                        print_val(f, find_depth(s, depths[di], sc ? MIX_DSCAN : MIX_DGET, old));
                fprintf(f, "\n");
            }
        }
    }

    /* Version memory */
    fprintf(f, "\n## Version memory (mv)\n\n| structure | keys | most version records in one repetition | MiB "
               "(64 B each) |\n|---|---|---:|---:|\n");
    for (int s = S_MV_FLAT; s <= S_MV_ROWS; s++)
        for (int ki = 0; ki < opt.nkeys; ki++) {
            uint64_t v = 0;
            int any = 0;
            for (size_t i = 0; i < nres; i++)
                if (res[i].ran && res[i].st == s && res[i].n == opt.keys[ki] && res[i].depth == 0) {
                    any = 1;
                    v = res[i].versions > v ? res[i].versions : v;
                }
            if (!any)
                continue;
            fmt_keys(kb, sizeof(kb), opt.keys[ki]);
            fprintf(f, "| %s | %s | %llu | %.1f |\n", st_names[s], kb, (unsigned long long)v,
                    (double)v * 64.0 / (double)MIB);
        }
    fprintf(f, "\nEvery update of an existing key moves the previous version into one 64 B record; a load into an "
               "empty table needs none.\n");
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
        fprintf(f, "structure,page_mode,keys,threads,dist,mix,depth,snapshot,mops,mkeys,p50_ns,p99_ns,p999_ns,"
                   "bad_value,bad_status,bad_scan,chains_bad,count_ok,versions,mem_plan_bytes,mem_measured_bytes\n");
    for (size_t i = 0; i < nres; i++) {
        const Result *r = &res[i];
        if (!r->ran)
            continue;
        fprintf(f, "%s,%s,%llu,%llu,%s,%s,%llu,%s,%.3f,%.3f,%.0f,%.0f,%.0f,%llu,%llu,%llu,%llu,%d,%llu,%llu,%llu\n",
                st_names[r->st], page_names[opt.page_mode], (unsigned long long)r->n, (unsigned long long)r->threads,
                r->dist < 0 ? "-" : dist_names[r->dist], mix_names[r->mix], (unsigned long long)r->depth,
                r->depth ? (r->old ? "oldest" : "newest") : "newest", r->mops, r->mkeys, r->p50, r->p99, r->p999,
                (unsigned long long)r->bad_value, (unsigned long long)r->bad_status, (unsigned long long)r->bad_scan,
                (unsigned long long)r->chains_bad, r->count_ok, (unsigned long long)r->versions,
                (unsigned long long)r->mem_plan, (unsigned long long)r->mem_measured);
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
            fprintf(stderr, "ERROR: no 2 MiB hugetlb pages available (%s). Use scripts/scale/run_mv_scale.sh, which "
                            "reserves them, or --pages base.\n",
                    strerror(errno));
            return 2;
        }
        vdc_unmap(p, VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB);
    }

    /* Plan every (N, structure) first. */
    int runnable[MAX_LIST][S_COUNT];
    uint64_t pages_needed = 0;
    for (int ki = 0; ki < opt.nkeys; ki++)
        for (int si = 0; si < opt.nstructs; si++) {
            const int s = opt.structs[si];
            const uint64_t n = opt.keys[ki];
            runnable[ki][si] = 0;
            int any_t = 0;
            for (int ti = 0; ti < opt.nthreads; ti++)
                any_t |= runs_struct(s, n, opt.threads[ti]);
            if (!any_t)
                continue; /* skip list at this N / thread list: not planned */
            Result r;
            memset(&r, 0, sizeof(r));
            r.st = s;
            r.n = n;
            r.mem_plan = plan_total(s, n);
            const uint64_t pages = opt.page_mode == VDC_PAGES_HUGETLB ? mapped_bytes(s, n) / VDC_HUGE_PAGE_SIZE : 0;
            if (r.mem_plan > budget)
                snprintf(r.reason, sizeof(r.reason), "⏭ skipped: needs %.2f GiB, budget %.2f GiB", gib(r.mem_plan),
                         gib(budget));
            else if (!opt.plan_only && opt.page_mode == VDC_PAGES_HUGETLB && huge_free >= 0 &&
                     pages > (uint64_t)huge_free)
                snprintf(r.reason, sizeof(r.reason), "⏭ skipped: needs %llu huge pages, %ld free",
                         (unsigned long long)pages, huge_free);
            else {
                snprintf(r.reason, sizeof(r.reason), "✅ %s: %.2f GiB of %.2f GiB", opt.plan_only ? "will run" : "ran",
                         gib(r.mem_plan), gib(budget));
                runnable[ki][si] = 1;
                pages_needed = pages > pages_needed ? pages : pages_needed;
            }
            add_res(&r);
        }
    if (opt.plan_only) {
        print_report();
        fprintf(stderr, "HUGEPAGES_NEEDED=%llu\n", (unsigned long long)pages_needed);
        return 0;
    }

    fprintf(stderr, "== experiment 7: versions and range scans (budget %.2f GiB, %s pages)\n", gib(budget),
            page_names[opt.page_mode]);
    for (int ki = 0; ki < opt.nkeys; ki++) {
        const uint64_t n = opt.keys[ki];
        int any = 0;
        for (int si = 0; si < opt.nstructs; si++)
            any |= runnable[ki][si];
        if (!any)
            continue;
        char kb[32];
        fmt_keys(kb, sizeof(kb), n);
        const uint64_t tp = bench_now_ns();
        uint32_t *load_keys = malloc(n * sizeof(uint32_t));
        if (load_keys == NULL)
            die_oom();
        DatPerm perm;
        dat_perm_init(&perm, n, opt.seed);
        for (uint64_t i = 0; i < n; i++)
            load_keys[i] = (uint32_t)dat_perm(&perm, i);
        Zipf z;
        zipf_init(&z, n, 0.99);
        fprintf(stderr, "  %s keys: inputs ready in %.1f s\n", kb, (double)(bench_now_ns() - tp) / 1e9);

        for (int ti = 0; ti < opt.nthreads; ti++) {
            const uint64_t T = opt.threads[ti];
            int any_t = 0;
            for (int si = 0; si < opt.nstructs; si++)
                any_t |= runnable[ki][si] && runs_struct(opt.structs[si], n, T);
            if (!any_t)
                continue;
            make_work(n, T, &z);
            for (int si = 0; si < opt.nstructs; si++) {
                const int s = opt.structs[si];
                if (!runnable[ki][si] || !runs_struct(s, n, T))
                    continue;
                Store st;
                if (st_init(&st, s, n) != MT_OK) {
                    fprintf(stderr, "  %s %s: could not allocate (%s), skipped\n", st_names[s], kb, strerror(errno));
                    continue;
                }
                check_memory();
                int loaded = 0;
                for (int w = 0; w < nwl; w++) {
                    if (wl_mix[w] == MIX_LOAD) {
                        CellSpec cs = {MIX_LOAD, -1, NULL, DV_LATEST, 0, 0, 0, 0, 0};
                        run_cell(&st, n, T, &cs, load_keys);
                        loaded = 1;
                        continue;
                    }
                    if (!loaded) { /* no load workload requested: fill untimed */
                        uint64_t v[4];
                        for (uint64_t k = 0; k < n; k++) {
                            const uint64_t seq = __atomic_add_fetch(&g_seq, 1, __ATOMIC_RELAXED);
                            make_value(v, (uint32_t)k, seq);
                            st_put(&st, (uint32_t)k, v, seq);
                        }
                        loaded = 1;
                    }
                    int combo = -1;
                    for (int cc = 0; cc < ncombos; cc++)
                        if (combo_mix[cc] == wl_mix[w] && combo_dist[cc] == wl_dist[w])
                            combo = cc;
                    CellSpec cs = {wl_mix[w], wl_dist[w], &work_lists[(uint64_t)combo * T], DV_LATEST, 0, 0, 0, 0, 0};
                    run_cell(&st, n, T, &cs, load_keys);
                }
                if (is_depth_cell(s, n) && T == opt.depth_threads)
                    run_depth_sweep(&st, n, T);
                st_destroy(&st);
            }
            free_work(T);
        }
        free(load_keys);
    }
    if (opt.depth_keys && !has_thread(opt.depth_threads))
        fprintf(stderr, "note: --depth-threads %llu is not in --threads; no depth sweep ran\n",
                (unsigned long long)opt.depth_threads);

    print_report();
    if (opt.csv != NULL)
        write_csv();
    int failed = 0;
    for (size_t i = 0; i < nres; i++)
        failed |= res[i].ran &&
                  (res[i].bad_value || res[i].bad_status || res[i].bad_scan || res[i].chains_bad || res[i].count_ok == 0);
    if (failed)
        fprintf(stderr, "ERROR: wrong results detected (see the Checks table)\n");
    free(res);
    return failed ? 1 : 0;
}
