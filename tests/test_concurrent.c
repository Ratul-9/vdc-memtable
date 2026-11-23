/*
 * Concurrency tests: lock-free queues (exactly-once delivery), the five
 * thread-safe memtables (no torn reads, no lost writes) and the three
 * delegation services (results, ownership, per-owner ordering).
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "tests.h"
#include "vdc/concurrent.h"
#include "vdc/queue.h"
#include "vdc/service.h"

#define NTHREADS 4

/* A value whose four 64-bit words are equal: a torn read shows mixed words. */
static void make_value(uint64_t w[4], uint32_t key, uint32_t ver)
{
    const uint64_t x = ((uint64_t)key << 32) | ver;
    w[0] = w[1] = w[2] = w[3] = x;
}

static int value_ok(const uint64_t w[4], uint32_t key)
{
    return w[0] == w[1] && w[1] == w[2] && w[2] == w[3] && (uint32_t)(w[0] >> 32) == key;
}

/* ---------------- queue ---------------- */

enum { Q_PER_PRODUCER = 100000, Q_PRODUCERS = 4, Q_CONSUMERS = 4 };

typedef struct {
    VdcQueue *q;
    int id;
    unsigned char *seen;
    int bad;
    uint64_t *popped;
} QArg;

static void *q_producer(void *p)
{
    QArg *a = p;
    VdcRequest r;
    memset(&r, 0, sizeof(r));
    for (uint32_t i = 0; i < Q_PER_PRODUCER; i++) {
        r.key = (uint32_t)a->id * Q_PER_PRODUCER + i;
        r.seqno = r.key;
        while (vq_push(a->q, &r) != 0)
            vdc_cpu_relax();
    }
    return NULL;
}

static void *q_consumer(void *p)
{
    QArg *a = p;
    VdcRequest r;
    const uint64_t total = (uint64_t)Q_PER_PRODUCER * Q_PRODUCERS;
    while (__atomic_load_n(a->popped, __ATOMIC_RELAXED) < total) {
        if (vq_pop_mc(a->q, &r) != 0) {
            vdc_cpu_relax();
            continue;
        }
        if (r.key >= total || r.seqno != r.key || __atomic_exchange_n(&a->seen[r.key], (unsigned char)1, __ATOMIC_RELAXED))
            a->bad = 1; /* corrupt or delivered twice */
        __atomic_fetch_add(a->popped, 1, __ATOMIC_RELAXED);
    }
    return NULL;
}

static int test_queue(void)
{
    VdcQueue q;
    VdcRequest r, out;
    memset(&r, 0, sizeof(r));

    CHECK(vq_init(&q, 3) != 0); /* not a power of two */
    CHECK(vq_init(&q, 8) == 0);
    for (uint32_t i = 0; i < 8; i++) {
        r.key = i;
        CHECK(vq_push(&q, &r) == 0);
    }
    CHECK(vq_push(&q, &r) != 0); /* full */
    CHECK(vq_size(&q) == 8);
    for (uint32_t i = 0; i < 8; i++)
        CHECK(vq_pop_sc(&q, &out) == 0 && out.key == i); /* FIFO */
    CHECK(vq_pop_sc(&q, &out) != 0); /* empty */
    vq_destroy(&q);

    /* MPMC: 4 producers, 4 consumers, every item exactly once. */
    CHECK(vq_init(&q, 1024) == 0);
    const size_t total = (size_t)Q_PER_PRODUCER * Q_PRODUCERS;
    unsigned char *seen = calloc(total, 1);
    uint64_t popped = 0;
    CHECK(seen != NULL);
    pthread_t pt[Q_PRODUCERS], ct[Q_CONSUMERS];
    QArg pa[Q_PRODUCERS], ca[Q_CONSUMERS];
    for (int i = 0; i < Q_CONSUMERS; i++) {
        ca[i] = (QArg){&q, i, seen, 0, &popped};
        pthread_create(&ct[i], NULL, q_consumer, &ca[i]);
    }
    for (int i = 0; i < Q_PRODUCERS; i++) {
        pa[i] = (QArg){&q, i, seen, 0, &popped};
        pthread_create(&pt[i], NULL, q_producer, &pa[i]);
    }
    for (int i = 0; i < Q_PRODUCERS; i++)
        pthread_join(pt[i], NULL);
    for (int i = 0; i < Q_CONSUMERS; i++)
        pthread_join(ct[i], NULL);
    int bad = 0;
    for (int i = 0; i < Q_CONSUMERS; i++)
        bad |= ca[i].bad;
    size_t n = 0;
    for (size_t i = 0; i < total; i++)
        n += seen[i];
    free(seen);
    vq_destroy(&q);
    CHECK(!bad);
    CHECK(n == total);
    return 0;
}

/* ---------------- thread-safe memtables ---------------- */

enum { CM_KEYS_PER_THREAD = 20000, HOT_KEYS = 64, HOT_OPS = 60000 };

typedef struct {
    CMemtable *cm;
    int id;
    int writer;
    int bad;
    int *stop;
} CmArg;

/* Disjoint keys: thread t owns keys t, t+4, t+8, ... */
static void *cm_disjoint(void *p)
{
    CmArg *a = p;
    uint64_t w[4];
    for (uint32_t i = 0; i < CM_KEYS_PER_THREAD; i++) {
        const uint32_t key = i * NTHREADS + (uint32_t)a->id;
        make_value(w, key, 1);
        if (cm_put(a->cm, key, w, sizeof(w), key) != MT_OK)
            a->bad = 1;
    }
    return NULL;
}

/* Hot keys: writers overwrite 64 keys, readers check every value is whole. */
static void *cm_hot(void *p)
{
    CmArg *a = p;
    uint64_t w[4];
    if (a->writer) {
        for (uint32_t i = 0; i < HOT_OPS; i++) {
            const uint32_t key = i % HOT_KEYS;
            make_value(w, key, i * NTHREADS + (uint32_t)a->id);
            if (cm_put(a->cm, key, w, sizeof(w), i) != MT_OK)
                a->bad = 1;
        }
    } else {
        uint32_t i = 0;
        while (!__atomic_load_n(a->stop, __ATOMIC_ACQUIRE)) {
            const uint32_t key = (i++) % HOT_KEYS;
            uint32_t len = 0;
            memset(w, 0, sizeof(w));
            const int rc = cm_get(a->cm, key, w, &len, NULL);
            if (rc != MT_OK || len != sizeof(w) || !value_ok(w, key))
                a->bad = 1;
        }
    }
    return NULL;
}

static int test_cmemtable_mode(CmMode mode)
{
    CMemtable cm;
    CHECK(cm_init(&cm, mode, 11, VDC_PAGES_BASE, 0) == MT_OK);
    pthread_t t[NTHREADS + 2];
    CmArg a[NTHREADS + 2];
    uint64_t w[4];
    int stop = 0;

    /* Disjoint inserts, then every key must hold its value. */
    for (int i = 0; i < NTHREADS; i++) {
        a[i] = (CmArg){&cm, i, 1, 0, &stop};
        pthread_create(&t[i], NULL, cm_disjoint, &a[i]);
    }
    for (int i = 0; i < NTHREADS; i++) {
        pthread_join(t[i], NULL);
        CHECK(!a[i].bad);
    }
    CHECK(cm_count(&cm, NULL) == NTHREADS * CM_KEYS_PER_THREAD);
    for (uint32_t k = 0; k < NTHREADS * CM_KEYS_PER_THREAD; k++) {
        uint32_t len;
        CHECK(cm_get(&cm, k, w, &len, NULL) == MT_OK && value_ok(w, k));
    }

    /* Hot keys: 4 writers + 2 readers on 64 keys. */
    for (uint32_t k = 0; k < HOT_KEYS; k++) {
        make_value(w, k, 0);
        CHECK(cm_put(&cm, k, w, sizeof(w), 0) == MT_OK);
    }
    for (int i = 0; i < NTHREADS + 2; i++) {
        a[i] = (CmArg){&cm, i, i < NTHREADS, 0, &stop};
        pthread_create(&t[i], NULL, cm_hot, &a[i]);
    }
    for (int i = 0; i < NTHREADS; i++)
        pthread_join(t[i], NULL);
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    for (int i = NTHREADS; i < NTHREADS + 2; i++)
        pthread_join(t[i], NULL);
    for (int i = 0; i < NTHREADS + 2; i++)
        CHECK(!a[i].bad);

    /* Delete, errors, clear. */
    CHECK(cm_delete(&cm, 5, 99) == MT_OK);
    CHECK(cm_get(&cm, 5, w, NULL, NULL) == MT_DELETED);
    CHECK(cm_get(&cm, VDC_NUM_SLOTS - 1, w, NULL, NULL) == MT_NOT_FOUND);
    CHECK(cm_put(&cm, VDC_NUM_SLOTS, w, 8, 0) == MT_ERR_KEY_RANGE);
    CHECK(cm_put(&cm, 1, w, 33, 0) == MT_ERR_ARG);
    cm_clear(&cm);
    CHECK(cm_count(&cm, NULL) == 0);
    cm_destroy(&cm);
    return 0;
}

static int test_cmemtable(void)
{
    for (int m = 0; m < CM_MODE_COUNT; m++) {
        if (test_cmemtable_mode((CmMode)m) != 0) {
            fprintf(stderr, "    (mode %s)\n", cm_mode_name((CmMode)m));
            return 1;
        }
    }
    CMemtable cm;
    CHECK(cm_init(&cm, CM_MODE_COUNT, 11, VDC_PAGES_BASE, 0) == MT_ERR_ARG);
    return 0;
}

/* ---------------- delegation services ---------------- */

enum { SV_KEYS_PER_PRODUCER = 5000, SV_ASYNC_WINDOW = 16 };

typedef struct {
    VdcService *sv;
    int id;
    int bad;
} SvArg;

/* Sync: put, get, delete odd keys, get again. Producer p owns keys p, p+4, ... */
static void *sv_sync_producer(void *p)
{
    SvArg *a = p;
    uint64_t w[4];
    for (uint32_t i = 0; i < SV_KEYS_PER_PRODUCER; i++) {
        const uint32_t key = i * NTHREADS + (uint32_t)a->id;
        uint32_t len = 0;
        make_value(w, key, 1);
        if (sv_put(a->sv, key, w, sizeof(w), 1) != MT_OK)
            a->bad = 1;
        memset(w, 0, sizeof(w));
        if (sv_get(a->sv, key, w, &len, NULL) != MT_OK || len != sizeof(w) || !value_ok(w, key))
            a->bad = 1;
        if (key & 1u) {
            if (sv_delete(a->sv, key, 2) != MT_OK || sv_get(a->sv, key, w, NULL, NULL) != MT_DELETED)
                a->bad = 1;
        }
    }
    return NULL;
}

/* Async: two puts per key in flight (version 1 then 2); the owner applies them in order. */
static void *sv_async_producer(void *p)
{
    SvArg *a = p;
    VdcCompletion *c = aligned_alloc(64, 2 * SV_ASYNC_WINDOW * sizeof(VdcCompletion));
    if (c == NULL) {
        a->bad = 1;
        return NULL;
    }
    memset(c, 0, 2 * SV_ASYNC_WINDOW * sizeof(VdcCompletion));
    VdcRequest r;
    memset(&r, 0, sizeof(r));
    r.op = VDC_OP_PUT;
    r.len = 32;
    for (uint32_t i = 0; i < SV_KEYS_PER_PRODUCER; i++) {
        const uint32_t key = i * NTHREADS + (uint32_t)a->id;
        const uint32_t s = (i % SV_ASYNC_WINDOW) * 2;
        if (i >= SV_ASYNC_WINDOW) /* reuse slots only when their requests are done */
            for (int k = 0; k < 2; k++)
                while (!__atomic_load_n(&c[s + k].done, __ATOMIC_ACQUIRE))
                    vdc_cpu_relax();
        for (uint32_t ver = 1; ver <= 2; ver++) {
            uint64_t w[4];
            make_value(w, key, ver);
            memcpy(r.value, w, sizeof(w));
            r.key = key;
            r.seqno = ver;
            c[s + ver - 1].done = 0;
            r.done = &c[s + ver - 1];
            if (sv_submit(a->sv, &r) != MT_OK)
                a->bad = 1;
        }
    }
    /* Every in-flight request must finish before its completion slot is freed. */
    const uint32_t used = SV_KEYS_PER_PRODUCER < SV_ASYNC_WINDOW ? SV_KEYS_PER_PRODUCER : SV_ASYNC_WINDOW;
    for (uint32_t k = 0; k < 2 * used; k++) {
        while (!__atomic_load_n(&c[k].done, __ATOMIC_ACQUIRE))
            vdc_cpu_relax();
        if (c[k].status != MT_OK)
            a->bad = 1;
    }
    free(c);
    return NULL;
}

static int run_producers(VdcService *sv, void *(*fn)(void *))
{
    pthread_t t[NTHREADS];
    SvArg a[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) {
        a[i] = (SvArg){sv, i, 0};
        pthread_create(&t[i], NULL, fn, &a[i]);
    }
    int bad = 0;
    for (int i = 0; i < NTHREADS; i++) {
        pthread_join(t[i], NULL);
        bad |= a[i].bad;
    }
    return bad;
}

static int test_service_mode(SvMode mode)
{
    SvConfig cfg = {mode, 3, 11, VDC_PAGES_BASE, 256, NULL};
    VdcService *sv = sv_create(&cfg);
    CHECK(sv != NULL);
    CHECK(sv_consumers(sv) == (mode == SV_MPSC ? 1u : 3u));
    const uint32_t nkeys = SV_KEYS_PER_PRODUCER * NTHREADS;

    /* Sync round trips from 4 producers. */
    CHECK(run_producers(sv, sv_sync_producer) == 0);
    sv_quiesce(sv);
    uint32_t tomb = 0;
    CHECK(sv_count(sv, &tomb) == nkeys);
    CHECK(tomb == nkeys / 2);

    /* Async, two writes per key in flight. Per-owner FIFO keeps version 2 last,
     * except in the shared MPMC design, where forwarding may reorder them. */
    sv_clear(sv);
    CHECK(sv_count(sv, NULL) == 0);
    CHECK(run_producers(sv, sv_async_producer) == 0);
    sv_quiesce(sv);
    CHECK(sv_count(sv, NULL) == nkeys);
    uint32_t last = 0;
    for (uint32_t k = 0; k < nkeys; k++) {
        uint64_t w[4];
        CHECK(sv_get(sv, k, w, NULL, NULL) == MT_OK && value_ok(w, k));
        last += ((uint32_t)w[0] == 2);
    }
    if (mode != SV_SHARED_MPMC)
        CHECK(last == nkeys);

    /* Ownership is a partition of the key domain into contiguous ranges. */
    CHECK(sv_owner(sv, 0) == 0);
    CHECK(sv_owner(sv, VDC_NUM_SLOTS - 1) == sv_consumers(sv) - 1);
    CHECK(sv_put(sv, VDC_NUM_SLOTS, NULL, 0, 0) == MT_ERR_KEY_RANGE);
    sv_destroy(sv);
    return 0;
}

static int test_service(void)
{
    for (int m = 0; m < SV_MODE_COUNT; m++) {
        if (test_service_mode((SvMode)m) != 0) {
            fprintf(stderr, "    (mode %s)\n", sv_mode_name((SvMode)m));
            return 1;
        }
    }
    SvConfig bad = {SV_SHARDED, 0, 11, VDC_PAGES_BASE, 256, NULL};
    CHECK(sv_create(&bad) == NULL);
    return 0;
}

int test_concurrent(void)
{
    CHECK(test_queue() == 0);
    CHECK(test_cmemtable() == 0);
    CHECK(test_service() == 0);
    return 0;
}
