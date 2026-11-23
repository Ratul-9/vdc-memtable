#include "vdc/concurrent.h"

#include <stdlib.h>
#include <string.h>

#include "node_ops.h"
#include "vdc/platform.h"

static const char *const mode_names[CM_MODE_COUNT] = {"mutex", "spin", "rwlock", "nodelock", "seqlock"};

const char *cm_mode_name(CmMode mode)
{
    return (mode >= 0 && mode < CM_MODE_COUNT) ? mode_names[mode] : "?";
}

/* ---------------- lock primitives ---------------- */

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

/* Seqlock writer: wait for an even version, CAS it to odd. Returns the even value. */
static inline uint32_t seq_write_begin(Node *n)
{
    for (;;) {
        uint32_t v = __atomic_load_n(&n->version, __ATOMIC_RELAXED);
        if (!(v & 1u) &&
            __atomic_compare_exchange_n(&n->version, &v, v + 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return v;
        vdc_cpu_relax();
    }
}

static inline void seq_write_end(Node *n, uint32_t v) { __atomic_store_n(&n->version, v + 2, __ATOMIC_RELEASE); }

/* Seqlock reader: copy the Node until the version is even and unchanged. */
static inline void seq_read(const Node *n, NodeSnap *s)
{
    for (;;) {
        const uint32_t v1 = __atomic_load_n(&n->version, __ATOMIC_ACQUIRE);
        if (v1 & 1u) {
            vdc_cpu_relax();
            continue;
        }
        nops_snap(n, s);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&n->version, __ATOMIC_RELAXED) == v1)
            return;
        vdc_cpu_relax();
    }
}

/* ---------------- memtable ---------------- */

int cm_init(CMemtable *cm, CmMode mode, unsigned j, VdcPageMode page_mode, int populate)
{
    if (cm == NULL)
        return MT_ERR_ARG;
    memset(cm, 0, sizeof(*cm));
    if (mode < 0 || mode >= CM_MODE_COUNT || j > VDC_KEY_BITS)
        return MT_ERR_ARG;

    if (vdc_space_init_ex(&cm->space, j, page_mode, populate) != 0)
        return MT_ERR_ALLOC;
    cm->mode = mode;
    pthread_mutex_init(&cm->mutex, NULL);

    if (mode == CM_SPIN || mode == CM_RWLOCK) {
        cm->nstripes = VDC_NUM_SLOTS >> VDC_STRIPE_SHIFT;
        cm->stripes = aligned_alloc(64, (size_t)cm->nstripes * sizeof(CmLock));
        if (cm->stripes == NULL) {
            pthread_mutex_destroy(&cm->mutex);
            vdc_space_destroy(&cm->space);
            memset(cm, 0, sizeof(*cm));
            return MT_ERR_ALLOC;
        }
        memset(cm->stripes, 0, (size_t)cm->nstripes * sizeof(CmLock));
    }
    return MT_OK;
}

void cm_destroy(CMemtable *cm)
{
    if (cm == NULL || cm->space.slots == NULL)
        return;
    free(cm->stripes);
    pthread_mutex_destroy(&cm->mutex);
    vdc_space_destroy(&cm->space);
    memset(cm, 0, sizeof(*cm));
}

void cm_clear(CMemtable *cm)
{
    if (cm == NULL)
        return;
    vdc_space_clear(&cm->space); /* also resets every lock byte and version */
    if (cm->stripes != NULL)
        memset(cm->stripes, 0, (size_t)cm->nstripes * sizeof(CmLock));
}

/* Write an entry under the scheme's exclusion. */
static int cm_write(CMemtable *cm, uint32_t key, const void *value, size_t len, uint64_t seqno, uint32_t flags)
{
    const uint32_t slot = vdc_space_slot_of(&cm->space, key);
    Node *n = &cm->space.slots[slot];

    switch (cm->mode) {
    case CM_MUTEX:
        pthread_mutex_lock(&cm->mutex);
        nops_write(n, key, value, len, seqno, flags);
        pthread_mutex_unlock(&cm->mutex);
        break;
    case CM_SPIN: {
        uint32_t *w = &cm->stripes[slot >> VDC_STRIPE_SHIFT].word;
        spin_lock(w);
        nops_write(n, key, value, len, seqno, flags);
        spin_unlock(w);
        break;
    }
    case CM_RWLOCK: {
        uint32_t *w = &cm->stripes[slot >> VDC_STRIPE_SHIFT].word;
        rw_write_lock(w);
        nops_write(n, key, value, len, seqno, flags);
        rw_write_unlock(w);
        break;
    }
    case CM_NODELOCK:
        byte_lock(&n->lock);
        nops_write(n, key, value, len, seqno, flags);
        byte_unlock(&n->lock);
        break;
    case CM_SEQLOCK: {
        const uint32_t v = seq_write_begin(n);
        nops_write(n, key, value, len, seqno, flags);
        seq_write_end(n, v);
        break;
    }
    default:
        return MT_ERR_ARG;
    }
    return MT_OK;
}

int cm_put(CMemtable *cm, uint32_t key, const void *value, size_t len, uint64_t seqno)
{
    if (key >= VDC_NUM_SLOTS)
        return MT_ERR_KEY_RANGE;
    if (len > VDC_NODE_VALUE_BYTES || (len > 0 && value == NULL))
        return MT_ERR_ARG;
    return cm_write(cm, key, value, len, seqno, NODE_F_OCCUPIED);
}

int cm_delete(CMemtable *cm, uint32_t key, uint64_t seqno)
{
    if (key >= VDC_NUM_SLOTS)
        return MT_ERR_KEY_RANGE;
    return cm_write(cm, key, NULL, 0, seqno, NODE_F_OCCUPIED | NODE_F_TOMBSTONE);
}

int cm_get(CMemtable *cm, uint32_t key, void *out, uint32_t *len, uint64_t *seqno)
{
    if (key >= VDC_NUM_SLOTS)
        return MT_ERR_KEY_RANGE;

    const uint32_t slot = vdc_space_slot_of(&cm->space, key);
    Node *n = &cm->space.slots[slot];
    NodeSnap s;

    switch (cm->mode) {
    case CM_MUTEX:
        pthread_mutex_lock(&cm->mutex);
        nops_snap(n, &s);
        pthread_mutex_unlock(&cm->mutex);
        break;
    case CM_SPIN: {
        uint32_t *w = &cm->stripes[slot >> VDC_STRIPE_SHIFT].word;
        spin_lock(w);
        nops_snap(n, &s);
        spin_unlock(w);
        break;
    }
    case CM_RWLOCK: {
        uint32_t *w = &cm->stripes[slot >> VDC_STRIPE_SHIFT].word;
        rw_read_lock(w);
        nops_snap(n, &s);
        rw_read_unlock(w);
        break;
    }
    case CM_NODELOCK:
        byte_lock(&n->lock);
        nops_snap(n, &s);
        byte_unlock(&n->lock);
        break;
    case CM_SEQLOCK:
        seq_read(n, &s);
        break;
    default:
        return MT_ERR_ARG;
    }
    return nops_result(&s, out, len, seqno);
}

uint32_t cm_count(const CMemtable *cm, uint32_t *tombstones)
{
    uint32_t n = 0, t = 0;
    for (uint32_t i = 0; i < VDC_NUM_SLOTS; i++) {
        const uint32_t f = cm->space.slots[i].flags;
        n += (f & NODE_F_OCCUPIED) != 0;
        t += (f & NODE_F_TOMBSTONE) != 0;
    }
    if (tombstones != NULL)
        *tombstones = t;
    return n;
}
