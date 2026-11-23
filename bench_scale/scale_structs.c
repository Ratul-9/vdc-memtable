/* Adapters: flat array, two-level rows, hash table, skip list, and key generation only. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "baselines.h"
#include "dat/dat_flat.h"
#include "dat/dat_rows.h"
#include "scale.h"

/* ---------------- flat ---------------- */

static int flat_get_copy(const DatFlat *f, uint32_t key, void *out, uint32_t *len)
{
    return dat_flat_get(f, key, out, len);
}

#define S_T DatFlat
#define S_PREFIX flat
#define S_PUT(t, k, v, l, s) dat_flat_put((t), (k), (v), (l), (s))
#define S_GET(t, k, o, l) flat_get_copy((t), (k), (o), (l))
#include "scale_phases.h"

static void *flat_create(const ScaleStruct *s, uint64_t n, uint64_t rows_needed)
{
    (void)rows_needed;
    DatFlat *f = malloc(sizeof(*f));
    if (f == NULL)
        return NULL;
    /* Base pages: reserve the whole key range, commit only touched pages.
     * Huge pages: commit exactly the dense key range (rounded to 2 MiB). */
    uint64_t cap = DAT_DOMAIN;
    if (s->mode == VDC_PAGES_HUGETLB) {
        const uint64_t per_page = VDC_HUGE_PAGE_SIZE / sizeof(DatNode);
        cap = (n + per_page - 1) / per_page * per_page;
    }
    if (dat_flat_init(f, cap, s->mode) != MT_OK) {
        const int e = errno;
        free(f);
        errno = e;
        return NULL;
    }
    return f;
}

static void flat_destroy(void *t)
{
    dat_flat_destroy((DatFlat *)t);
    free(t);
}

static void flat_prepare(void *t, const DatKeySet *k)
{
    DatFlat *f = (DatFlat *)t;
    if (!f->lazy)
        return;
    for (uint64_t i = 0; i < k->n; i++) /* write: commit the pages of the key set */
        dat_flat_touch(f, dat_key(k, i));
    uint32_t buf[8];
    for (uint64_t i = 0; i < k->n_miss; i++) /* read: map the shared zero page, no memory */
        (void)dat_flat_get(f, dat_miss_key(k, i), buf, NULL);
}

static void flat_clear(void *t, const DatKeySet *k)
{
    DatFlat *f = (DatFlat *)t;
    if (!f->lazy) {
        memset(f->nodes, 0, f->map_bytes);
        return;
    }
    for (uint64_t i = 0; i < k->n; i++)
        dat_flat_reset(f, dat_key(k, i));
}

static uint64_t flat_used(void *t)
{
    const DatFlat *f = (const DatFlat *)t;
    return f->lazy ? 0 : vdc_map_size(f->map_bytes, f->mode);
}

/* ---------------- rows ---------------- */

#define S_T DatRows
#define S_PREFIX rows
#define S_PUT(t, k, v, l, s) dat_rows_put((t), (k), (v), (l), (s))
#define S_GET(t, k, o, l) dat_rows_get((t), (k), (o), (l))
#include "scale_phases.h"

static void *rows_create(const ScaleStruct *s, uint64_t n, uint64_t rows_needed)
{
    (void)n;
    DatRows *r = malloc(sizeof(*r));
    if (r == NULL)
        return NULL;
    if (dat_rows_init(r, DAT_KEY_BITS, s->row_bits, rows_needed > 0 ? rows_needed : 1, s->mode) != MT_OK) {
        const int e = errno;
        free(r);
        errno = e;
        return NULL;
    }
    return r;
}

static void rows_destroy(void *t)
{
    dat_rows_destroy((DatRows *)t);
    free(t);
}

static void rows_prepare(void *t, const DatKeySet *k)
{
    (void)t;
    (void)k; /* fully committed at creation */
}

static void rows_clear(void *t, const DatKeySet *k)
{
    (void)k;
    dat_rows_clear((DatRows *)t);
}

static uint64_t rows_used(void *t) { return dat_rows_used_bytes((const DatRows *)t); }

/* ---------------- hash table ---------------- */

static int hash_get_copy(const HashTable *h, uint32_t key, void *out, uint32_t *len)
{
    const void *v;
    uint32_t l = 0;
    const int rc = ht_get(h, key, &v, &l);
    if (rc == MT_OK) {
        memcpy(out, v, l);
        if (len != NULL)
            *len = l;
    }
    return rc;
}

#define S_T HashTable
#define S_PREFIX hash
#define S_PUT(t, k, v, l, s) ht_put((t), (k), (v), (l))
#define S_GET(t, k, o, l) hash_get_copy((t), (k), (o), (l))
#include "scale_phases.h"

static void *hash_create(const ScaleStruct *s, uint64_t n, uint64_t rows_needed)
{
    (void)rows_needed;
    HashTable *h = malloc(sizeof(*h));
    if (h == NULL)
        return NULL;
    unsigned lg = 10;
    while ((((uint64_t)1 << lg) * 3) < n * 4) /* load factor <= 0.75 */
        lg++;
    if (ht_init(h, lg, s->mode, 1) != MT_OK) {
        const int e = errno;
        free(h);
        errno = e;
        return NULL;
    }
    return h;
}

static void hash_destroy(void *t)
{
    ht_destroy((HashTable *)t);
    free(t);
}

static void hash_prepare(void *t, const DatKeySet *k)
{
    (void)t;
    (void)k;
}

static void hash_clear(void *t, const DatKeySet *k)
{
    (void)k;
    ht_clear((HashTable *)t);
}

static uint64_t hash_used(void *t) { return ((uint64_t)((HashTable *)t)->mask + 1) * sizeof(HtEntry); }

/* ---------------- skip list ---------------- */

static int skip_get_copy(const SkipList *sl, uint32_t key, void *out, uint32_t *len)
{
    const void *v;
    uint32_t l = 0;
    const int rc = sl_get(sl, key, &v, &l);
    if (rc == MT_OK) {
        memcpy(out, v, l);
        if (len != NULL)
            *len = l;
    }
    return rc;
}

#define S_T SkipList
#define S_PREFIX skip
#define S_PUT(t, k, v, l, s) sl_put((t), (k), (v), (l))
#define S_GET(t, k, o, l) skip_get_copy((t), (k), (o), (l))
#include "scale_phases.h"

static void *skip_create(const ScaleStruct *s, uint64_t n, uint64_t rows_needed)
{
    (void)rows_needed;
    SkipList *sl = malloc(sizeof(*sl));
    if (sl == NULL)
        return NULL;
    if (sl_init(sl, n * 96 + 65536, s->mode, 1) != MT_OK) {
        const int e = errno;
        free(sl);
        errno = e;
        return NULL;
    }
    return sl;
}

static void skip_destroy(void *t)
{
    sl_destroy((SkipList *)t);
    free(t);
}

static void skip_prepare(void *t, const DatKeySet *k)
{
    (void)t;
    (void)k;
}

static void skip_clear(void *t, const DatKeySet *k)
{
    (void)k;
    sl_clear((SkipList *)t);
}

static uint64_t skip_used(void *t) { return ((SkipList *)t)->arena_bytes; }

/* ---------------- key generation only ---------------- */

static int g_keygen_token;

static void *keygen_create(const ScaleStruct *s, uint64_t n, uint64_t rows_needed)
{
    (void)s;
    (void)n;
    (void)rows_needed;
    return &g_keygen_token;
}

static void keygen_destroy(void *t) { (void)t; }
static void keygen_prepare(void *t, const DatKeySet *k)
{
    (void)t;
    (void)k;
}
static void keygen_clear(void *t, const DatKeySet *k)
{
    (void)t;
    (void)k;
}
static uint64_t keygen_used(void *t)
{
    (void)t;
    return 0;
}

/* Only computes the keys an insert pass would use. Reported for information: this
 * work happens between the measured sections of every other structure. */
static void keygen_phase(void *t, int op, const PhaseIn *in, PhaseOut *out)
{
    (void)t;
    uint64_t sum = 0, ops = 0;
    const uint64_t t0 = bench_now_ns();
    const DatKeySet *ks = in->keys;
    const int rnd = (in->order == ORD_RAND);
    if (op == PH_INSERT) {
        for (uint64_t i = 0; i < ks->n; i++)
            sum += dat_key(ks, rnd ? dat_perm(in->ord, i) : i);
        ops = ks->n;
    }
    memset(&out->rd, 0, sizeof(out->rd));
    out->ns = bench_now_ns() - t0;
    out->ops = ops;
    out->sum = sum;
    out->ok = 1;
}

static const ScaleOps ops_table[S_KIND_COUNT] = {
    [S_FLAT] = {flat_create, flat_destroy, flat_prepare, flat_clear, flat_phase, flat_used},
    [S_ROWS] = {rows_create, rows_destroy, rows_prepare, rows_clear, rows_phase, rows_used},
    [S_HASH] = {hash_create, hash_destroy, hash_prepare, hash_clear, hash_phase, hash_used},
    [S_SKIPLIST] = {skip_create, skip_destroy, skip_prepare, skip_clear, skip_phase, skip_used},
    [S_KEYGEN] = {keygen_create, keygen_destroy, keygen_prepare, keygen_clear, keygen_phase, keygen_used},
};

const ScaleOps *scale_ops(int kind) { return (kind >= 0 && kind < S_KIND_COUNT) ? &ops_table[kind] : NULL; }
