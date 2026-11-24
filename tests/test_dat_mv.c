/*
 * Multi-version DAT with range scans (dat_mv.h), both layouts.
 *   - versions at snapshots, tombstones, out-of-order (late) writes, depth;
 *   - scans: tombstones skipped, snapshots, limits and resuming, missing rows skipped;
 *   - drop_history, clear, arena full, argument errors;
 *   - 4 writers (seqnos from a shared counter, so late writes happen) + 2 snapshot
 *     readers + 1 scanner on 64 hot keys: no torn value, nothing newer than the
 *     snapshot; afterwards every chain is sorted, holds every write, and a get at
 *     any snapshot returns exactly the newest write <= snapshot;
 *   - 4 threads racing to create rows, then a full scan;
 *   - the skip-list scan baseline (sl_scan).
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "baselines.h"
#include "dat/dat_mv.h"
#include "tests.h"

#define TH 4
enum { HOT = 64, HOT_OPS = 20000, KEYS = 40000 };

static void mkval(uint64_t w[4], uint32_t key, uint64_t seq)
{
    w[0] = ((uint64_t)key << 32) | (uint32_t)seq;
    w[1] = seq;
    w[2] = key ^ seq;
    w[3] = ~seq;
}

static int val_ok(const uint64_t w[4], uint32_t key, uint64_t seq)
{
    return w[0] == (((uint64_t)key << 32) | (uint32_t)seq) && w[1] == seq && w[2] == (key ^ seq) && w[3] == ~seq;
}

static int get_seq(DatMV *m, uint32_t key, uint64_t snap, uint64_t *seq)
{
    uint64_t w[4];
    uint32_t len = 0;
    const int rc = dv_get(m, key, snap, w, &len, seq);
    if (rc == MT_OK && (len != sizeof(w) || !val_ok(w, key, *seq)))
        return -100;
    return rc;
}

static int put_v(DatMV *m, uint32_t key, uint64_t seq)
{
    uint64_t w[4];
    mkval(w, key, seq);
    return dv_put(m, key, w, 32, seq);
}

/* ---------------- single-threaded semantics ---------------- */

static int semantics(DcLayout layout)
{
    DatMV m;
    uint64_t w[4], s = 0;
    CHECK(dv_init(&m, layout, 10, 20000, 0, 10000, VDC_PAGES_BASE) == MT_OK);

    /* versions 10, 20, tombstone 30, 40 */
    CHECK(put_v(&m, 5, 10) == MT_OK);
    CHECK(put_v(&m, 5, 20) == MT_OK);
    CHECK(dv_delete(&m, 5, 30) == MT_OK);
    CHECK(put_v(&m, 5, 40) == MT_OK);
    CHECK(get_seq(&m, 5, 5, &s) == MT_NOT_FOUND);
    CHECK(get_seq(&m, 5, 10, &s) == MT_OK && s == 10);
    CHECK(get_seq(&m, 5, 19, &s) == MT_OK && s == 10);
    CHECK(get_seq(&m, 5, 20, &s) == MT_OK && s == 20);
    CHECK(get_seq(&m, 5, 35, &s) == MT_DELETED && s == 30);
    CHECK(get_seq(&m, 5, DV_LATEST, &s) == MT_OK && s == 40);
    CHECK(dv_depth(&m, 5) == 4);

    /* late writes land at their seqno position */
    CHECK(put_v(&m, 5, 25) == MT_OK);
    CHECK(put_v(&m, 5, 1) == MT_OK);
    CHECK(put_v(&m, 5, 35) == MT_OK);
    CHECK(get_seq(&m, 5, 24, &s) == MT_OK && s == 20);
    CHECK(get_seq(&m, 5, 29, &s) == MT_OK && s == 25);
    CHECK(get_seq(&m, 5, 5, &s) == MT_OK && s == 1);
    CHECK(get_seq(&m, 5, 37, &s) == MT_OK && s == 35);
    CHECK(get_seq(&m, 5, DV_LATEST, &s) == MT_OK && s == 40);
    CHECK(dv_depth(&m, 5) == 7);
    uint64_t chained = 0;
    CHECK(dv_check(&m, &chained) == 0 && chained == 6);

    /* scans: keys 100..199 (seq = key), even keys deleted at 1000 */
    for (uint32_t k = 100; k < 200; k++)
        CHECK(put_v(&m, k, k) == MT_OK);
    for (uint32_t k = 100; k < 200; k += 2)
        CHECK(dv_delete(&m, k, 1000) == MT_OK);
    DatKV kv[128];
    uint32_t next = 0;
    CHECK(dv_scan(&m, 100, 200, 128, DV_LATEST, kv, &next) == 50 && next == 200);
    for (int i = 0; i < 50; i++)
        CHECK(kv[i].key == 101u + 2u * (uint32_t)i && kv[i].seqno == kv[i].key &&
              val_ok((const uint64_t *)(const void *)kv[i].value, kv[i].key, kv[i].seqno));
    CHECK(dv_scan(&m, 100, 200, 128, 999, kv, NULL) == 100);
    CHECK(dv_scan(&m, 100, 200, 128, 150, kv, NULL) == 51 && kv[50].key == 150);
    CHECK(dv_scan(&m, 0, 200, 128, 3, kv, NULL) == 1 && kv[0].key == 5 && kv[0].seqno == 1);
    /* limit + resume */
    size_t total = 0;
    uint32_t from = 100;
    while (from < 200) {
        const size_t got = dv_scan(&m, from, 200, 7, DV_LATEST, kv, &from);
        CHECK(got <= 7);
        total += got;
    }
    CHECK(total == 50);
    CHECK(dv_scan(&m, 300, 250, 10, DV_LATEST, kv, &next) == 0);

    /* drop_history keeps the newest version only */
    dv_drop_history(&m);
    CHECK(dv_versions_used(&m) == 0 && dv_depth(&m, 5) == 1);
    CHECK(get_seq(&m, 5, DV_LATEST, &s) == MT_OK && s == 40);
    CHECK(get_seq(&m, 5, 39, &s) == MT_NOT_FOUND);
    CHECK(get_seq(&m, 100, DV_LATEST, &s) == MT_DELETED);
    CHECK(put_v(&m, 5, 50) == MT_OK);
    CHECK(get_seq(&m, 5, 45, &s) == MT_OK && s == 40);
    CHECK(dv_check(&m, &chained) == 0 && chained == 1);

    /* errors */
    CHECK(dv_put(&m, 1, w, 33, 1) == MT_ERR_ARG);
    CHECK(dv_put(&m, (uint32_t)DAT_DOMAIN, w, 8, 1) == MT_ERR_KEY_RANGE);
    CHECK(dv_get(&m, (uint32_t)DAT_DOMAIN, DV_LATEST, w, NULL, NULL) == MT_ERR_KEY_RANGE);
    if (layout == DC_ROWS) {
        CHECK(dv_get(&m, 5000000, DV_LATEST, w, NULL, NULL) == MT_NOT_FOUND); /* row never created */
        /* scan over rows that do not exist: keys 0..9 and 19000..19009 */
        dv_clear(&m);
        for (uint32_t k = 0; k < 10; k++)
            CHECK(put_v(&m, k, 1) == MT_OK);
        for (uint32_t k = 19000; k < 19010; k++)
            CHECK(put_v(&m, k, 1) == MT_OK);
        CHECK(m.rows.used_rows == 2);
        CHECK(dv_scan(&m, 0, (uint32_t)DAT_DOMAIN, 128, DV_LATEST, kv, &next) == 20 && next == (uint32_t)DAT_DOMAIN);
        CHECK(kv[9].key == 9 && kv[10].key == 19000);
    } else {
        CHECK(dv_put(&m, (uint32_t)m.flat.capacity, w, 8, 1) == MT_ERR_KEY_RANGE);
        CHECK(dv_scan(&m, 0, (uint32_t)DAT_DOMAIN, 128, DV_LATEST, kv, &next) == 51 && next == (uint32_t)DAT_DOMAIN);
    }
    dv_clear(&m);
    CHECK(dv_count(&m) == 0 && dv_versions_used(&m) == 0);
    CHECK(get_seq(&m, 5, DV_LATEST, &s) == MT_NOT_FOUND);
    dv_destroy(&m);

    /* version arena full: nothing changes */
    CHECK(dv_init(&m, layout, 10, 1000, 0, 3, VDC_PAGES_BASE) == MT_OK);
    for (uint64_t q = 1; q <= 4; q++)
        CHECK(put_v(&m, 9, q) == MT_OK);
    CHECK(put_v(&m, 9, 5) == MT_ERR_ARENA_FULL);
    CHECK(get_seq(&m, 9, DV_LATEST, &s) == MT_OK && s == 4 && dv_depth(&m, 9) == 4);
    CHECK(put_v(&m, 8, 1) == MT_OK); /* a new key needs no record */
    dv_drop_history(&m);
    CHECK(put_v(&m, 9, 6) == MT_OK);
    dv_destroy(&m);

    CHECK(dv_init(&m, layout, 10, 1000, 0, 0, VDC_PAGES_BASE) == MT_ERR_ARG);
    return 0;
}

/* ---------------- concurrency ---------------- */

typedef struct {
    DatMV *m;
    int id;
    int role; /* 0 writer, 1 reader, 2 scanner */
    int bad;
    int *stop;
    uint64_t *clock;
    uint64_t *log; /* writer: seqno of op i (key = i % HOT) */
} Arg;

static void *worker(void *p)
{
    Arg *a = p;
    uint64_t w[4];
    uint64_t rng = 0x9E3779B97F4A7C15ull * (uint64_t)(a->id + 1);
    if (a->role == 0) {
        for (uint32_t i = 0; i < HOT_OPS; i++) {
            const uint32_t k = (i * 7u + (uint32_t)a->id) % HOT;
            const uint64_t seq = __atomic_add_fetch(a->clock, 1, __ATOMIC_RELAXED);
            a->log[i] = seq;
            int rc;
            if (i % 16 == 15) {
                rc = dv_delete(a->m, k, seq);
            } else {
                mkval(w, k, seq);
                rc = dv_put(a->m, k, w, 32, seq);
            }
            if (rc != MT_OK)
                a->bad = 1;
        }
    } else if (a->role == 1) {
        while (!__atomic_load_n(a->stop, __ATOMIC_ACQUIRE)) {
            rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
            const uint32_t k = (uint32_t)(rng % HOT);
            const uint64_t now = __atomic_load_n(a->clock, __ATOMIC_RELAXED);
            const uint64_t snap = (rng & 1) ? DV_LATEST : (rng >> 8) % (now + 1);
            uint64_t s = 0;
            const int rc = get_seq(a->m, k, snap, &s);
            if (rc == -100 || ((rc == MT_OK || rc == MT_DELETED) && s > snap))
                a->bad = 1;
        }
    } else {
        DatKV kv[HOT];
        while (!__atomic_load_n(a->stop, __ATOMIC_ACQUIRE)) {
            const uint64_t snap = __atomic_load_n(a->clock, __ATOMIC_RELAXED);
            const size_t got = dv_scan(a->m, 0, HOT, HOT, snap, kv, NULL);
            for (size_t i = 0; i < got; i++)
                if (kv[i].seqno > snap || (i > 0 && kv[i].key <= kv[i - 1].key) ||
                    !val_ok((const uint64_t *)(const void *)kv[i].value, kv[i].key, kv[i].seqno))
                    a->bad = 1;
        }
    }
    return NULL;
}

static int concurrent(DcLayout layout)
{
    DatMV m;
    CHECK(dv_init(&m, layout, 6, HOT, 0, (uint64_t)TH * HOT_OPS + 16 * DV_CHUNK, VDC_PAGES_BASE) == MT_OK);
    uint64_t *log = calloc((size_t)TH * HOT_OPS, sizeof(uint64_t));
    CHECK(log != NULL);
    pthread_t t[TH + 3];
    Arg a[TH + 3];
    int stop = 0;
    uint64_t clock = 0;
    for (int i = 0; i < TH + 3; i++) {
        a[i] = (Arg){&m, i, i < TH ? 0 : (i < TH + 2 ? 1 : 2), 0, &stop, &clock, log + (size_t)(i < TH ? i : 0) * HOT_OPS};
        pthread_create(&t[i], NULL, worker, &a[i]);
    }
    for (int i = 0; i < TH; i++)
        pthread_join(t[i], NULL);
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    for (int i = TH; i < TH + 3; i++)
        pthread_join(t[i], NULL);
    for (int i = 0; i < TH + 3; i++)
        CHECK(!a[i].bad);

    /* every write is in a sorted chain */
    uint64_t chained = 0, depth = 0;
    CHECK(dv_check(&m, &chained) == 0);
    CHECK(chained == (uint64_t)TH * HOT_OPS - HOT);
    for (uint32_t k = 0; k < HOT; k++)
        depth += dv_depth(&m, k);
    CHECK(depth == (uint64_t)TH * HOT_OPS);

    /* a get at any snapshot returns the newest write <= snapshot */
    uint64_t rng = 12345;
    for (int probe = 0; probe < 2000; probe++) {
        rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
        const uint32_t k = (uint32_t)(rng % HOT);
        const uint64_t snap = (rng >> 8) % (clock + 2);
        uint64_t best = 0;
        int best_del = 0;
        for (int th = 0; th < TH; th++)
            for (uint32_t i = 0; i < HOT_OPS; i++)
                if ((i * 7u + (uint32_t)th) % HOT == k && log[(size_t)th * HOT_OPS + i] <= snap &&
                    log[(size_t)th * HOT_OPS + i] > best) {
                    best = log[(size_t)th * HOT_OPS + i];
                    best_del = i % 16 == 15;
                }
        uint64_t s = 0;
        const int rc = get_seq(&m, k, snap, &s);
        if (best == 0)
            CHECK(rc == MT_NOT_FOUND);
        else
            CHECK(rc == (best_del ? MT_DELETED : MT_OK) && s == best);
    }
    free(log);
    dv_destroy(&m);
    return 0;
}

typedef struct {
    DatMV *m;
    int id;
    int bad;
} RArg;

static void *insert_interleaved(void *p)
{
    RArg *a = p;
    uint64_t w[4];
    for (uint32_t k = (uint32_t)a->id; k < KEYS; k += TH) {
        mkval(w, k, k + 1);
        if (dv_put(a->m, k, w, 32, k + 1) != MT_OK)
            a->bad = 1;
    }
    return NULL;
}

static int row_race(DcLayout layout)
{
    DatMV m;
    CHECK(dv_init(&m, layout, 10, KEYS, 0, 1024, VDC_PAGES_BASE) == MT_OK);
    pthread_t t[TH];
    RArg a[TH];
    for (int i = 0; i < TH; i++) {
        a[i] = (RArg){&m, i, 0};
        pthread_create(&t[i], NULL, insert_interleaved, &a[i]);
    }
    for (int i = 0; i < TH; i++) {
        pthread_join(t[i], NULL);
        CHECK(!a[i].bad);
    }
    CHECK(dv_count(&m) == KEYS);
    if (layout == DC_ROWS)
        CHECK(m.rows.used_rows == (KEYS + 1023) / 1024);
    DatKV *kv = malloc(sizeof(DatKV) * KEYS);
    CHECK(kv != NULL);
    uint32_t next = 0;
    CHECK(dv_scan(&m, 0, (uint32_t)DAT_DOMAIN, KEYS, DV_LATEST, kv, &next) == KEYS);
    for (uint32_t k = 0; k < KEYS; k++)
        CHECK(kv[k].key == k && kv[k].seqno == k + 1);
    free(kv);
    dv_destroy(&m);
    return 0;
}

static int skiplist_scan(void)
{
    SkipList sl;
    CHECK(sl_init(&sl, 1 << 20, VDC_PAGES_BASE, 1) == MT_OK);
    uint64_t w[4] = {0, 0, 0, 0};
    for (uint32_t k = 0; k < 1000; k += 3)
        CHECK(sl_put(&sl, k, w, 32) == MT_OK);
    CHECK(sl_delete(&sl, 6) == MT_OK);
    uint32_t keys[64];
    const void *vals[64];
    CHECK(sl_scan(&sl, 1, 20, 64, keys, vals) == 5); /* 3 9 12 15 18 */
    CHECK(keys[0] == 3 && keys[1] == 9 && keys[4] == 18 && vals[0] != NULL);
    CHECK(sl_scan(&sl, 0, 1000, 10, keys, NULL) == 10 && keys[9] == 30);
    CHECK(sl_scan(&sl, 1000, 2000, 10, keys, NULL) == 0);
    sl_destroy(&sl);
    return 0;
}

int test_dat_mv(void)
{
    for (int l = 0; l < DC_LAYOUT_COUNT; l++) {
        const char *name = l == DC_FLAT ? "flat" : "rows";
        if (semantics((DcLayout)l) != 0) {
            fprintf(stderr, "    (semantics, %s)\n", name);
            return 1;
        }
        if (concurrent((DcLayout)l) != 0) {
            fprintf(stderr, "    (concurrent, %s)\n", name);
            return 1;
        }
        if (row_race((DcLayout)l) != 0) {
            fprintf(stderr, "    (row race, %s)\n", name);
            return 1;
        }
    }
    return skiplist_scan();
}
