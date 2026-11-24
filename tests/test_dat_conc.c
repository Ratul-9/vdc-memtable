/*
 * Thread-safe DAT memtables (dat_conc.h): every layout x lock mode.
 *   - 4 threads insert interleaved keys, so they race to create the same rows;
 *     afterwards every key is present and the arena holds exactly the rows needed.
 *   - a full scan returns every key once, in order; limits, tombstones;
 *   - 4 writers + 2 readers on 64 hot keys: no torn or misrouted value is ever read.
 */
#include <pthread.h>
#include <string.h>

#include "dat/dat_conc.h"
#include "tests.h"

#define TH 4
enum { KEYS = 40000, HOT = 64, HOT_OPS = 40000 };

static void make_value(uint64_t w[4], uint32_t key, uint32_t ver)
{
    const uint64_t x = ((uint64_t)key << 32) | ver;
    w[0] = w[1] = w[2] = w[3] = x;
}

static int value_ok(const uint64_t w[4], uint32_t key)
{
    return w[0] == w[1] && w[1] == w[2] && w[2] == w[3] && (uint32_t)(w[0] >> 32) == key;
}

typedef struct {
    DatConc *c;
    int id;
    int writer;
    int bad;
    int *stop;
} Arg;

/* Thread t inserts keys t, t+4, t+8, ...: neighbours, so every row is raced for. */
static void *insert_interleaved(void *p)
{
    Arg *a = p;
    uint64_t w[4];
    for (uint32_t k = (uint32_t)a->id; k < KEYS; k += TH) {
        make_value(w, k, 1);
        if (dc_put(a->c, k, w, sizeof(w), k) != MT_OK)
            a->bad = 1;
    }
    return NULL;
}

static void *hot(void *p)
{
    Arg *a = p;
    uint64_t w[4];
    if (a->writer) {
        for (uint32_t i = 0; i < HOT_OPS; i++) {
            const uint32_t k = i % HOT;
            make_value(w, k, i * TH + (uint32_t)a->id);
            if (dc_put(a->c, k, w, sizeof(w), i) != MT_OK)
                a->bad = 1;
        }
    } else {
        uint32_t i = 0;
        while (!__atomic_load_n(a->stop, __ATOMIC_ACQUIRE)) {
            const uint32_t k = (i++) % HOT;
            uint32_t len = 0;
            memset(w, 0, sizeof(w));
            if (dc_get(a->c, k, w, &len, NULL) != MT_OK || len != sizeof(w) || !value_ok(w, k))
                a->bad = 1;
        }
    }
    return NULL;
}

static int one(DcLayout layout, DcMode mode)
{
    DatConc c;
    const unsigned row_bits = 10; /* 1,024 keys per row: 40,000 keys need 40 rows */
    CHECK(dc_init(&c, layout, mode, row_bits, KEYS, 0, VDC_PAGES_BASE) == MT_OK);
    pthread_t t[TH + 2];
    Arg a[TH + 2];
    int stop = 0;
    uint64_t w[4];

    for (int i = 0; i < TH; i++) {
        a[i] = (Arg){&c, i, 1, 0, &stop};
        pthread_create(&t[i], NULL, insert_interleaved, &a[i]);
    }
    for (int i = 0; i < TH; i++) {
        pthread_join(t[i], NULL);
        CHECK(!a[i].bad);
    }
    CHECK(dc_count(&c) == KEYS);
    if (layout == DC_ROWS)
        CHECK(c.rows.used_rows == (KEYS + 1023) / 1024); /* no row created twice, none wasted */
    for (uint32_t k = 0; k < KEYS; k++) {
        uint32_t len = 0;
        CHECK(dc_get(&c, k, w, &len, NULL) == MT_OK && value_ok(w, k));
    }

    { /* scan everything: every key once, ascending */
        static DatKV kv[KEYS];
        uint32_t next = 0;
        CHECK(dc_scan(&c, 0, (uint32_t)DAT_DOMAIN, KEYS, kv, &next) == KEYS && next == KEYS); /* limit reached: resume at KEYS */
        for (uint32_t k = 0; k < KEYS; k++)
            CHECK(kv[k].key == k && kv[k].len == 32 && value_ok((const uint64_t *)(const void *)kv[k].value, k));
        CHECK(dc_scan(&c, 100, 200, 10, kv, &next) == 10 && next == 110 && kv[9].key == 109);
    }
    for (int i = 0; i < TH + 2; i++) {
        a[i] = (Arg){&c, i, i < TH, 0, &stop};
        pthread_create(&t[i], NULL, hot, &a[i]);
    }
    for (int i = 0; i < TH; i++)
        pthread_join(t[i], NULL);
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    for (int i = TH; i < TH + 2; i++)
        pthread_join(t[i], NULL);
    for (int i = 0; i < TH + 2; i++)
        CHECK(!a[i].bad);

    CHECK(dc_delete(&c, 7, 1) == MT_OK);
    CHECK(dc_get(&c, 7, w, NULL, NULL) == MT_DELETED);
    {
        DatKV kv[16];
        CHECK(dc_scan(&c, 0, 10, 16, kv, NULL) == 9 && kv[6].key == 6 && kv[7].key == 8); /* tombstone skipped */
    }
    CHECK(dc_put(&c, 1, w, 33, 0) == MT_ERR_ARG);
    if (layout == DC_FLAT) {
        CHECK(dc_put(&c, (uint32_t)c.flat.capacity, w, 8, 0) == MT_ERR_KEY_RANGE);
    } else {
        CHECK(dc_get(&c, 5000000, w, NULL, NULL) == MT_NOT_FOUND);          /* row never created */
        CHECK(dc_put(&c, 5000000, w, 8, 0) == MT_ERR_ARENA_FULL);           /* arena sized exactly */
        CHECK(dc_put(&c, (uint32_t)DAT_DOMAIN, w, 8, 0) == MT_ERR_KEY_RANGE);
    }
    dc_clear(&c);
    CHECK(dc_count(&c) == 0);
    CHECK(dc_get(&c, 1, w, NULL, NULL) == MT_NOT_FOUND);
    CHECK(dc_put(&c, 1, w, 8, 0) == MT_OK);
    dc_destroy(&c);
    return 0;
}

int test_dat_conc(void)
{
    for (int l = 0; l < DC_LAYOUT_COUNT; l++)
        for (int m = 0; m < DC_MODE_COUNT; m++)
            if (one((DcLayout)l, (DcMode)m) != 0) {
                fprintf(stderr, "    (%s, %s)\n", l == DC_FLAT ? "flat" : "rows", dc_mode_name((DcMode)m));
                return 1;
            }
    DatConc c;
    CHECK(dc_init(&c, DC_ROWS, DC_SEQLOCK, 0, 10, 0, VDC_PAGES_BASE) == MT_ERR_ARG);
    CHECK(dc_init(&c, DC_FLAT, DC_MODE_COUNT, 0, 10, 0, VDC_PAGES_BASE) == MT_ERR_ARG);
    return 0;
}
