#include "dat/dat_conc.h"

#include <stdlib.h>
#include <string.h>

static const char *const mode_names[DC_MODE_COUNT] = {"mutex", "spin", "rwlock", "nodelock", "seqlock"};

const char *dc_mode_name(DcMode m) { return (m >= 0 && m < DC_MODE_COUNT) ? mode_names[m] : "?"; }

/* ---------------- node copies with relaxed atomic words (race-free for optimistic readers) ---------------- */

typedef struct DnSnap {
    uint32_t flags;
    uint32_t len;
    uint64_t seqno;
    uint64_t w[4];
} DnSnap;

static inline void dn_write(DatNode *n, uint32_t key, const void *value, size_t len, uint64_t seqno, uint32_t flags)
{
    uint64_t w[4] = {0, 0, 0, 0};
    if (len > 0)
        memcpy(w, value, len);
    __atomic_store_n(&n->key, key, __ATOMIC_RELAXED);
    __atomic_store_n(&n->seqno, seqno, __ATOMIC_RELAXED);
    __atomic_store_n(&n->val_len, (uint16_t)len, __ATOMIC_RELAXED);
    for (int i = 0; i < 4; i++)
        __atomic_store_n((uint64_t *)(void *)(n->value + 8 * i), w[i], __ATOMIC_RELAXED);
    __atomic_store_n(&n->flags, flags, __ATOMIC_RELAXED);
}

static inline void dn_snap(const DatNode *n, DnSnap *s)
{
    s->flags = __atomic_load_n(&n->flags, __ATOMIC_RELAXED);
    s->len = __atomic_load_n(&n->val_len, __ATOMIC_RELAXED);
    s->seqno = __atomic_load_n(&n->seqno, __ATOMIC_RELAXED);
    for (int i = 0; i < 4; i++)
        s->w[i] = __atomic_load_n((const uint64_t *)(const void *)(n->value + 8 * i), __ATOMIC_RELAXED);
}

static inline int dn_result(const DnSnap *s, void *out, uint32_t *len, uint64_t *seqno)
{
    if (!(s->flags & DAT_F_OCCUPIED))
        return MT_NOT_FOUND;
    if (seqno != NULL)
        *seqno = s->seqno;
    if (s->flags & DAT_F_TOMBSTONE)
        return MT_DELETED;
    const uint32_t l = s->len > DAT_VALUE_BYTES ? DAT_VALUE_BYTES : s->len;
    if (out != NULL && l > 0)
        memcpy(out, s->w, l);
    if (len != NULL)
        *len = l;
    return MT_OK;
}

/* ---------------- locks ---------------- */

static inline void spin_lock(uint32_t *w)
{
    for (;;) {
        if (__atomic_load_n(w, __ATOMIC_RELAXED) == 0 && __atomic_exchange_n(w, 1u, __ATOMIC_ACQUIRE) == 0)
            return;
        vdc_cpu_relax();
    }
}
static inline void spin_unlock(uint32_t *w) { __atomic_store_n(w, 0u, __ATOMIC_RELEASE); }

#define RW_WRITER 0x80000000u
static inline void rw_read_lock(uint32_t *w)
{
    for (;;) {
        uint32_t v = __atomic_load_n(w, __ATOMIC_RELAXED);
        if (!(v & RW_WRITER) && __atomic_compare_exchange_n(w, &v, v + 1, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
        vdc_cpu_relax();
    }
}
static inline void rw_read_unlock(uint32_t *w) { __atomic_fetch_sub(w, 1u, __ATOMIC_RELEASE); }
static inline void rw_write_lock(uint32_t *w)
{
    for (;;) {
        uint32_t v = 0;
        if (__atomic_compare_exchange_n(w, &v, RW_WRITER, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
        vdc_cpu_relax();
    }
}
static inline void rw_write_unlock(uint32_t *w) { __atomic_store_n(w, 0u, __ATOMIC_RELEASE); }

static inline void byte_lock(uint8_t *b)
{
    for (;;) {
        if (__atomic_load_n(b, __ATOMIC_RELAXED) == 0 && __atomic_exchange_n(b, (uint8_t)1, __ATOMIC_ACQUIRE) == 0)
            return;
        vdc_cpu_relax();
    }
}
static inline void byte_unlock(uint8_t *b) { __atomic_store_n(b, (uint8_t)0, __ATOMIC_RELEASE); }

/* Seqlock writer: one fetch_or claims the node when the version was even (no CAS loop). */
static inline uint32_t seq_claim(DatNode *n)
{
    for (;;) {
        const uint32_t v = __atomic_fetch_or(&n->version, 1u, __ATOMIC_ACQUIRE);
        if (!(v & 1u))
            return v;
        while (__atomic_load_n(&n->version, __ATOMIC_RELAXED) & 1u)
            vdc_cpu_relax();
    }
}
static inline void seq_release(DatNode *n, uint32_t v) { __atomic_store_n(&n->version, v + 2, __ATOMIC_RELEASE); }

static inline void seq_read(const DatNode *n, DnSnap *s)
{
    for (;;) {
        const uint32_t v1 = __atomic_load_n(&n->version, __ATOMIC_ACQUIRE);
        if (v1 & 1u) {
            vdc_cpu_relax();
            continue;
        }
        dn_snap(n, s);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&n->version, __ATOMIC_RELAXED) == v1)
            return;
        vdc_cpu_relax();
    }
}

/* ---------------- node lookup ---------------- */

static inline int key_in_range(const DatConc *c, uint32_t key)
{
    if (c->layout == DC_FLAT)
        return key < c->flat.capacity;
    return ((uint64_t)key >> c->rows.domain_bits) == 0;
}

/* Node of key for reading; NULL if its row does not exist (yet). */
static inline DatNode *node_for_read(const DatConc *c, uint32_t key)
{
    if (c->layout == DC_FLAT)
        return &c->flat.nodes[key];
    const uint32_t e = __atomic_load_n(&c->rows.table[key >> c->rows.row_bits], __ATOMIC_ACQUIRE);
    if (e == 0 || e == DC_ROW_BUSY)
        return NULL;
    return dat_rows_node(&c->rows, e, key);
}

/* Node of key for writing; creates the row if needed. NULL = arena full. */
static DatNode *node_for_write(DatConc *c, uint32_t key)
{
    if (c->layout == DC_FLAT)
        return &c->flat.nodes[key];
    DatRows *r = &c->rows;
    uint32_t *slot = &r->table[key >> r->row_bits];
    uint32_t e = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    while (e == 0 || e == DC_ROW_BUSY) {
        if (e == 0) {
            uint32_t expected = 0;
            if (__atomic_compare_exchange_n(slot, &expected, DC_ROW_BUSY, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                const uint64_t idx = __atomic_fetch_add(&r->used_rows, 1, __ATOMIC_RELAXED);
                if (idx >= r->arena_rows) {
                    __atomic_fetch_sub(&r->used_rows, 1, __ATOMIC_RELAXED);
                    __atomic_store_n(slot, 0u, __ATOMIC_RELEASE);
                    return NULL;
                }
                e = (uint32_t)(idx + 1);
                __atomic_store_n(slot, e, __ATOMIC_RELEASE); /* the arena row is already zero */
                break;
            }
            e = expected;
            continue;
        }
        vdc_cpu_relax(); /* another thread is creating this row */
        e = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    }
    return dat_rows_node(r, e, key);
}

static inline uint32_t *stripe_of(DatConc *c, uint32_t key)
{
    return &c->stripes[(key >> DC_STRIPE_SHIFT) & (DC_STRIPES - 1)].word;
}

/* ---------------- public API ---------------- */

int dc_init(DatConc *c, DcLayout layout, DcMode mode, unsigned row_bits, uint64_t n_keys, uint64_t max_rows,
            VdcPageMode page_mode)
{
    if (c == NULL)
        return MT_ERR_ARG;
    memset(c, 0, sizeof(*c));
    if (layout < 0 || layout >= DC_LAYOUT_COUNT || mode < 0 || mode >= DC_MODE_COUNT || n_keys == 0 ||
        n_keys > DAT_DOMAIN)
        return MT_ERR_ARG;
    c->layout = layout;
    c->mode = mode;

    if (layout == DC_FLAT) {
        const uint64_t per = VDC_HUGE_PAGE_SIZE / sizeof(DatNode);
        uint64_t cap = (n_keys + per - 1) / per * per;
        if (cap > DAT_DOMAIN)
            cap = DAT_DOMAIN;
        const int rc = dat_flat_init(&c->flat, cap, page_mode);
        if (rc != MT_OK)
            return rc;
        if (c->flat.lazy) /* commit now, so no page faults while measuring */
            memset(c->flat.nodes, 0, (size_t)cap * sizeof(DatNode));
    } else {
        if (row_bits == 0 || row_bits > 20)
            return MT_ERR_ARG;
        if (max_rows == 0)
            max_rows = (n_keys + ((uint64_t)1 << row_bits) - 1) >> row_bits;
        const int rc = dat_rows_init(&c->rows, DAT_KEY_BITS, row_bits, max_rows, page_mode);
        if (rc != MT_OK)
            return rc;
    }

    pthread_mutex_init(&c->mutex, NULL);
    c->stripes = aligned_alloc(64, (size_t)DC_STRIPES * sizeof(DcLock));
    if (c->stripes == NULL) {
        dc_destroy(c);
        return MT_ERR_ALLOC;
    }
    memset(c->stripes, 0, (size_t)DC_STRIPES * sizeof(DcLock));
    return MT_OK;
}

void dc_destroy(DatConc *c)
{
    if (c == NULL)
        return;
    if (c->layout == DC_FLAT)
        dat_flat_destroy(&c->flat);
    else
        dat_rows_destroy(&c->rows);
    if (c->stripes != NULL) {
        free(c->stripes);
        pthread_mutex_destroy(&c->mutex);
    }
    memset(c, 0, sizeof(*c));
}

void dc_clear(DatConc *c)
{
    if (c->layout == DC_FLAT)
        memset(c->flat.nodes, 0, (size_t)c->flat.capacity * sizeof(DatNode));
    else
        dat_rows_clear(&c->rows);
    if (c->stripes != NULL)
        memset(c->stripes, 0, (size_t)DC_STRIPES * sizeof(DcLock));
}

static int dc_write(DatConc *c, uint32_t key, const void *value, size_t len, uint64_t seqno, uint32_t flags)
{
    if (!key_in_range(c, key))
        return MT_ERR_KEY_RANGE;
    DatNode *n = node_for_write(c, key);
    if (n == NULL)
        return MT_ERR_ARENA_FULL;

    switch (c->mode) {
    case DC_MUTEX:
        pthread_mutex_lock(&c->mutex);
        dn_write(n, key, value, len, seqno, flags);
        pthread_mutex_unlock(&c->mutex);
        break;
    case DC_SPIN: {
        uint32_t *w = stripe_of(c, key);
        spin_lock(w);
        dn_write(n, key, value, len, seqno, flags);
        spin_unlock(w);
        break;
    }
    case DC_RWLOCK: {
        uint32_t *w = stripe_of(c, key);
        rw_write_lock(w);
        dn_write(n, key, value, len, seqno, flags);
        rw_write_unlock(w);
        break;
    }
    case DC_NODELOCK:
        byte_lock(&n->lock);
        dn_write(n, key, value, len, seqno, flags);
        byte_unlock(&n->lock);
        break;
    case DC_SEQLOCK: {
        const uint32_t v = seq_claim(n);
        dn_write(n, key, value, len, seqno, flags);
        seq_release(n, v);
        break;
    }
    default:
        return MT_ERR_ARG;
    }
    return MT_OK;
}

int dc_put(DatConc *c, uint32_t key, const void *value, size_t len, uint64_t seqno)
{
    if (len > DAT_VALUE_BYTES || (len > 0 && value == NULL))
        return MT_ERR_ARG;
    return dc_write(c, key, value, len, seqno, DAT_F_OCCUPIED);
}

int dc_delete(DatConc *c, uint32_t key, uint64_t seqno)
{
    return dc_write(c, key, NULL, 0, seqno, DAT_F_OCCUPIED | DAT_F_TOMBSTONE);
}

int dc_get(DatConc *c, uint32_t key, void *out, uint32_t *len, uint64_t *seqno)
{
    if (!key_in_range(c, key))
        return MT_ERR_KEY_RANGE;
    const DatNode *n = node_for_read(c, key);
    if (n == NULL)
        return MT_NOT_FOUND;

    DnSnap s;
    switch (c->mode) {
    case DC_MUTEX:
        pthread_mutex_lock(&c->mutex);
        dn_snap(n, &s);
        pthread_mutex_unlock(&c->mutex);
        break;
    case DC_SPIN: {
        uint32_t *w = stripe_of(c, key);
        spin_lock(w);
        dn_snap(n, &s);
        spin_unlock(w);
        break;
    }
    case DC_RWLOCK: {
        uint32_t *w = stripe_of(c, key);
        rw_read_lock(w);
        dn_snap(n, &s);
        rw_read_unlock(w);
        break;
    }
    case DC_NODELOCK:
        byte_lock((uint8_t *)&((DatNode *)n)->lock);
        dn_snap(n, &s);
        byte_unlock((uint8_t *)&((DatNode *)n)->lock);
        break;
    case DC_SEQLOCK:
        seq_read(n, &s);
        break;
    default:
        return MT_ERR_ARG;
    }
    return dn_result(&s, out, len, seqno);
}

uint64_t dc_count(const DatConc *c)
{
    uint64_t n = 0;
    if (c->layout == DC_FLAT) {
        for (uint64_t i = 0; i < c->flat.capacity; i++)
            n += (c->flat.nodes[i].flags & DAT_F_OCCUPIED) != 0;
    } else {
        const uint64_t nodes = c->rows.used_rows << c->rows.row_bits;
        for (uint64_t i = 0; i < nodes; i++)
            n += (c->rows.arena[i].flags & DAT_F_OCCUPIED) != 0;
    }
    return n;
}

uint64_t dc_bytes(const DatConc *c)
{
    uint64_t b = (uint64_t)DC_STRIPES * sizeof(DcLock);
    if (c->layout == DC_FLAT)
        b += vdc_map_size(c->flat.map_bytes, c->flat.mode);
    else
        b += vdc_map_size(c->rows.table_bytes, c->rows.mode) + vdc_map_size(c->rows.arena_bytes, c->rows.mode);
    return b;
}
