#include "dat/dat_mv.h"

#include <string.h>

#include "dat_internal.h"

/* ---------------- node / record access ---------------- */

typedef struct MvSnap {
    uint32_t flags;
    uint32_t len;
    uint64_t seqno;
    uint64_t older;
    uint64_t w[4];
} MvSnap;

static inline void mv_node_write(DatNode *n, uint32_t key, const void *value, size_t len, uint64_t seqno,
                                 uint32_t flags)
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

/* Optimistic (seqlock) copy of the node, including the chain link. */
static inline void mv_node_read(const DatNode *n, MvSnap *s)
{
    for (;;) {
        const uint32_t v1 = __atomic_load_n(&n->version, __ATOMIC_ACQUIRE);
        if (v1 & 1u) {
            vdc_cpu_relax();
            continue;
        }
        s->flags = __atomic_load_n(&n->flags, __ATOMIC_RELAXED);
        s->len = __atomic_load_n(&n->val_len, __ATOMIC_RELAXED);
        s->seqno = __atomic_load_n(&n->seqno, __ATOMIC_RELAXED);
        s->older = __atomic_load_n(&n->older, __ATOMIC_RELAXED);
        for (int i = 0; i < 4; i++)
            s->w[i] = __atomic_load_n((const uint64_t *)(const void *)(n->value + 8 * i), __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&n->version, __ATOMIC_RELAXED) == v1)
            return;
        vdc_cpu_relax();
    }
}

static inline void rec_fill(DatVersion *r, uint32_t key, const void *value, size_t len, uint64_t seqno,
                            uint32_t flags)
{
    r->seqno = seqno;
    r->key = key;
    r->flags = flags;
    r->val_len = (uint16_t)len;
    memset(r->value, 0, sizeof(r->value));
    if (len > 0)
        memcpy(r->value, value, len);
}

/* Fill *out from a visible version. MT_OK or MT_DELETED. */
static inline int vis_result(uint32_t flags, uint32_t vlen, uint64_t vseq, const void *vbytes, void *out,
                             uint32_t *len, uint64_t *seqno)
{
    if (seqno != NULL)
        *seqno = vseq;
    if (flags & DAT_F_TOMBSTONE)
        return MT_DELETED;
    const uint32_t l = vlen > DAT_VALUE_BYTES ? DAT_VALUE_BYTES : vlen;
    if (out != NULL && l > 0)
        memcpy(out, vbytes, l);
    if (len != NULL)
        *len = l;
    return MT_OK;
}

/*
 * The version of a node visible at snap: fills out/len/seqno. Returns MT_OK,
 * MT_DELETED or MT_NOT_FOUND.
 */
static inline int mv_resolve(const DatMV *m, const DatNode *n, uint64_t snap, void *out, uint32_t *len,
                             uint64_t *seqno)
{
    MvSnap s;
    mv_node_read(n, &s);
    if (!(s.flags & DAT_F_OCCUPIED))
        return MT_NOT_FOUND;
    if (s.seqno <= snap)
        return vis_result(s.flags, s.len, s.seqno, s.w, out, len, seqno);
    for (uint64_t cur = s.older; cur != 0;) {
        const DatVersion *r = &m->versions[cur - 1];
        if (r->seqno <= snap)
            return vis_result(r->flags, r->val_len, r->seqno, r->value, out, len, seqno);
        cur = __atomic_load_n(&r->older, __ATOMIC_ACQUIRE);
    }
    return MT_NOT_FOUND;
}

/* ---------------- version allocation (per-thread chunks) ---------------- */

static uint64_t g_next_id = 1;

static __thread struct {
    uint64_t id, gen, next, end;
} tl_chunk;

/* Index of a free version record, or UINT64_MAX if the arena is full. */
static inline uint64_t valloc(DatMV *m)
{
    if (tl_chunk.id != m->id || tl_chunk.gen != m->gen || tl_chunk.next == tl_chunk.end) {
        const uint64_t base = __atomic_fetch_add(&m->vnext, DV_CHUNK, __ATOMIC_RELAXED);
        if (base >= m->vcap)
            return UINT64_MAX;
        tl_chunk.id = m->id;
        tl_chunk.gen = m->gen;
        tl_chunk.next = base;
        tl_chunk.end = base + DV_CHUNK < m->vcap ? base + DV_CHUNK : m->vcap;
    }
    return tl_chunk.next++;
}

/* ---------------- public API ---------------- */

int dv_init(DatMV *m, DcLayout layout, unsigned row_bits, uint64_t n_keys, uint64_t max_rows, uint64_t max_versions,
            VdcPageMode page_mode)
{
    if (m == NULL)
        return MT_ERR_ARG;
    memset(m, 0, sizeof(*m));
    if (layout < 0 || layout >= DC_LAYOUT_COUNT || n_keys == 0 || n_keys > DAT_DOMAIN || max_versions == 0 ||
        max_versions >= UINT64_MAX / 2)
        return MT_ERR_ARG;
    m->layout = layout;

    if (layout == DC_FLAT) {
        const uint64_t per = VDC_HUGE_PAGE_SIZE / sizeof(DatNode);
        uint64_t cap = (n_keys + per - 1) / per * per;
        if (cap > DAT_DOMAIN)
            cap = DAT_DOMAIN;
        const int rc = dat_flat_init(&m->flat, cap, page_mode);
        if (rc != MT_OK)
            return rc;
        if (m->flat.lazy) /* commit now, so no page faults while measuring */
            memset(m->flat.nodes, 0, (size_t)cap * sizeof(DatNode));
    } else {
        if (row_bits == 0 || row_bits > 20)
            return MT_ERR_ARG;
        if (max_rows == 0)
            max_rows = (n_keys + ((uint64_t)1 << row_bits) - 1) >> row_bits;
        const int rc = dat_rows_init(&m->rows, DAT_KEY_BITS, row_bits, max_rows, page_mode);
        if (rc != MT_OK)
            return rc;
    }

    m->vcap = max_versions;
    m->vbytes = (size_t)max_versions * sizeof(DatVersion);
    m->vmode = page_mode;
    m->versions = vdc_map(m->vbytes, page_mode, 1);
    if (m->versions == NULL) {
        dv_destroy(m);
        return MT_ERR_ALLOC;
    }
    m->id = __atomic_fetch_add(&g_next_id, 1, __ATOMIC_RELAXED);
    m->gen = 1;
    return MT_OK;
}

void dv_destroy(DatMV *m)
{
    if (m == NULL)
        return;
    if (m->layout == DC_FLAT)
        dat_flat_destroy(&m->flat);
    else
        dat_rows_destroy(&m->rows);
    if (m->versions != NULL)
        vdc_unmap(m->versions, m->vbytes, m->vmode);
    memset(m, 0, sizeof(*m));
}

void dv_clear(DatMV *m)
{
    if (m->layout == DC_FLAT)
        memset(m->flat.nodes, 0, (size_t)m->flat.capacity * sizeof(DatNode));
    else
        dat_rows_clear(&m->rows);
    m->vnext = 0;
    m->gen++;
}

static inline DatNode *all_nodes(const DatMV *m, uint64_t *count)
{
    if (m->layout == DC_FLAT) {
        *count = m->flat.capacity;
        return m->flat.nodes;
    }
    *count = m->rows.used_rows << m->rows.row_bits;
    return m->rows.arena;
}

void dv_drop_history(DatMV *m)
{
    uint64_t cnt;
    DatNode *nodes = all_nodes(m, &cnt);
    if (m->vnext != 0)
        for (uint64_t i = 0; i < cnt; i++)
            if (nodes[i].older != 0)
                nodes[i].older = 0;
    m->vnext = 0;
    m->gen++;
}

static int dv_write(DatMV *m, uint32_t key, const void *value, size_t len, uint64_t seqno, uint32_t flags)
{
    const int is_rows = m->layout == DC_ROWS;
    if (!dat_key_in_range(is_rows, &m->flat, &m->rows, key))
        return MT_ERR_KEY_RANGE;
    DatNode *n = dat_node_for_write(is_rows, &m->flat, &m->rows, key);
    if (n == NULL)
        return MT_ERR_ARENA_FULL;

    const uint32_t v = dat_seq_claim(n);
    int rc = MT_OK;
    if (!(n->flags & DAT_F_OCCUPIED)) {
        mv_node_write(n, key, value, len, seqno, flags);
    } else {
        const uint64_t idx = valloc(m);
        if (idx == UINT64_MAX) {
            rc = MT_ERR_ARENA_FULL;
        } else if (seqno >= n->seqno) {
            /* newest: move the inline version into the record, then overwrite inline */
            DatVersion *r = &m->versions[idx];
            rec_fill(r, n->key, n->value, n->val_len, n->seqno, n->flags);
            __atomic_store_n(&r->older, n->older, __ATOMIC_RELAXED);
            mv_node_write(n, key, value, len, seqno, flags);
            __atomic_store_n(&n->older, idx + 1, __ATOMIC_RELAXED); /* published by the seqlock release */
        } else {
            /* out of order: link a record at its seqno position (chain stays sorted) */
            DatVersion *r = &m->versions[idx];
            rec_fill(r, key, value, len, seqno, flags);
            uint64_t *link = &n->older;
            uint64_t cur = __atomic_load_n(link, __ATOMIC_RELAXED);
            while (cur != 0 && m->versions[cur - 1].seqno > seqno) {
                link = &m->versions[cur - 1].older;
                cur = __atomic_load_n(link, __ATOMIC_RELAXED);
            }
            __atomic_store_n(&r->older, cur, __ATOMIC_RELAXED);
            __atomic_store_n(link, idx + 1, __ATOMIC_RELEASE); /* the record is complete before it is reachable */
        }
    }
    dat_seq_release(n, v);
    return rc;
}

int dv_put(DatMV *m, uint32_t key, const void *value, size_t len, uint64_t seqno)
{
    if (len > DAT_VALUE_BYTES || (len > 0 && value == NULL))
        return MT_ERR_ARG;
    return dv_write(m, key, value, len, seqno, DAT_F_OCCUPIED);
}

int dv_delete(DatMV *m, uint32_t key, uint64_t seqno)
{
    return dv_write(m, key, NULL, 0, seqno, DAT_F_OCCUPIED | DAT_F_TOMBSTONE);
}

int dv_get(DatMV *m, uint32_t key, uint64_t snapshot, void *out, uint32_t *len, uint64_t *seqno)
{
    const int is_rows = m->layout == DC_ROWS;
    if (!dat_key_in_range(is_rows, &m->flat, &m->rows, key))
        return MT_ERR_KEY_RANGE;
    const DatNode *n = dat_node_for_read(is_rows, &m->flat, &m->rows, key);
    if (n == NULL)
        return MT_NOT_FOUND;
    return mv_resolve(m, n, snapshot, out, len, seqno);
}

/* Scan nodes[0 .. cnt-1] holding keys first .. first+cnt-1. Returns entries added. */
static inline size_t scan_nodes(const DatMV *m, const DatNode *nodes, uint32_t first, uint64_t cnt, uint64_t snap,
                                DatKV *out, size_t room, uint64_t *done)
{
    size_t got = 0;
    uint64_t i = 0;
    for (; i < cnt && got < room; i++) {
        const DatNode *n = &nodes[i];
        if (!(__atomic_load_n(&n->flags, __ATOMIC_RELAXED) & DAT_F_OCCUPIED))
            continue; /* empty slot: nothing to read */
        DatKV *kv = &out[got];
        if (mv_resolve(m, n, snap, kv->value, &kv->len, &kv->seqno) == MT_OK) {
            kv->key = first + (uint32_t)i;
            got++;
        }
    }
    *done = i;
    return got;
}

size_t dv_scan(DatMV *m, uint32_t start, uint32_t end, size_t limit, uint64_t snapshot, DatKV *out, uint32_t *next)
{
    size_t got = 0;
    uint64_t k = start;
    uint64_t hi = end;
    if (m->layout == DC_FLAT) {
        if (hi > m->flat.capacity)
            hi = m->flat.capacity;
        if (k < hi && limit > 0) {
            uint64_t done;
            got = scan_nodes(m, &m->flat.nodes[k], (uint32_t)k, hi - k, snapshot, out, limit, &done);
            k += done;
        }
    } else {
        const DatRows *r = &m->rows;
        const uint64_t dom = (uint64_t)1 << r->domain_bits;
        if (hi > dom)
            hi = dom;
        while (k < hi && got < limit) {
            const uint64_t row = k >> r->row_bits;
            const uint64_t row_end = (row + 1) << r->row_bits;
            const uint64_t stop = row_end < hi ? row_end : hi;
            const uint32_t e = __atomic_load_n(&r->table[row], __ATOMIC_ACQUIRE);
            if (e == 0 || e == DAT_ROW_BUSY) {
                k = stop; /* no row: skip all its keys */
                continue;
            }
            uint64_t done;
            got += scan_nodes(m, dat_rows_node(r, e, (uint32_t)k), (uint32_t)k, stop - k, snapshot, out + got,
                              limit - got, &done);
            k += done;
        }
    }
    if (next != NULL)
        *next = k < hi ? (uint32_t)k : end; /* keys past the table's range do not exist */
    return got;
}

uint64_t dv_count(const DatMV *m)
{
    uint64_t cnt, c = 0;
    const DatNode *nodes = all_nodes(m, &cnt);
    for (uint64_t i = 0; i < cnt; i++)
        c += (nodes[i].flags & DAT_F_OCCUPIED) != 0;
    return c;
}

uint64_t dv_versions_used(const DatMV *m)
{
    const uint64_t v = __atomic_load_n(&m->vnext, __ATOMIC_RELAXED);
    return v < m->vcap ? v : m->vcap;
}

uint64_t dv_check(const DatMV *m, uint64_t *chained)
{
    uint64_t cnt, bad = 0, linked = 0;
    const DatNode *nodes = all_nodes(m, &cnt);
    for (uint64_t i = 0; i < cnt; i++) {
        const DatNode *n = &nodes[i];
        if (!(n->flags & DAT_F_OCCUPIED))
            continue;
        uint64_t prev = n->seqno;
        int ok = 1;
        for (uint64_t cur = n->older; cur != 0; cur = m->versions[cur - 1].older) {
            const DatVersion *r = &m->versions[cur - 1];
            if (cur > m->vcap || r->seqno > prev || r->key != n->key) {
                ok = 0;
                break;
            }
            prev = r->seqno;
            linked++;
        }
        bad += !ok;
    }
    if (chained != NULL)
        *chained = linked;
    return bad;
}

uint64_t dv_depth(DatMV *m, uint32_t key)
{
    const int is_rows = m->layout == DC_ROWS;
    if (!dat_key_in_range(is_rows, &m->flat, &m->rows, key))
        return 0;
    const DatNode *n = dat_node_for_read(is_rows, &m->flat, &m->rows, key);
    if (n == NULL || !(n->flags & DAT_F_OCCUPIED))
        return 0;
    uint64_t d = 1;
    for (uint64_t cur = n->older; cur != 0; cur = m->versions[cur - 1].older)
        d++;
    return d;
}

uint64_t dv_bytes(const DatMV *m)
{
    uint64_t b = vdc_map_size(m->vbytes, m->vmode);
    if (m->layout == DC_FLAT)
        b += vdc_map_size(m->flat.map_bytes, m->flat.mode);
    else
        b += vdc_map_size(m->rows.table_bytes, m->rows.mode) + vdc_map_size(m->rows.arena_bytes, m->rows.mode);
    return b;
}
