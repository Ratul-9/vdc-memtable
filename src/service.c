#include "vdc/service.h"

#include <pthread.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#include "node_ops.h"
#include "vdc/vdc_space.h"

typedef struct SvConsumer {
    VdcQueue inbox; /* MPSC: requests (sharded / mpsc) or forwarded requests (shared mpmc) */
    struct VdcService *sv;
    pthread_t thread;
    unsigned id;
    int cpu;
    int started;
    alignas(64) uint64_t done;      /* requests popped and fully handled (written by this consumer only) */
    uint64_t forwarded;             /* requests passed to another consumer */
} SvConsumer;

struct VdcService {
    SvConfig cfg;
    VdcSpace space;
    VdcQueue shared; /* SV_SHARED_MPMC only */
    int has_shared;
    SvConsumer *cons;
    unsigned ncons;
    alignas(64) int stop;
};

static const char *const sv_names[SV_MODE_COUNT] = {"mpsc", "sharded", "mpmc"};

const char *sv_mode_name(SvMode mode)
{
    return (mode >= 0 && mode < SV_MODE_COUNT) ? sv_names[mode] : "?";
}

unsigned sv_owner(const VdcService *sv, uint32_t key)
{
    return (unsigned)(((uint64_t)key * sv->ncons) >> VDC_KEY_BITS);
}

unsigned sv_consumers(const VdcService *sv) { return sv->ncons; }

static void complete(VdcCompletion *d, int status)
{
    d->status = status;
    __atomic_store_n(&d->done, 1u, __ATOMIC_RELEASE);
}

/* Perform one request. Only the owner of r->key calls this, so no locking. */
static void apply(VdcService *sv, const VdcRequest *r)
{
    Node *n = vdc_space_node(&sv->space, r->key);
    int status = MT_OK;

    switch (r->op) {
    case VDC_OP_PUT:
        nops_write(n, r->key, r->value, r->len, r->seqno, NODE_F_OCCUPIED);
        break;
    case VDC_OP_DELETE:
        nops_write(n, r->key, NULL, 0, r->seqno, NODE_F_OCCUPIED | NODE_F_TOMBSTONE);
        break;
    case VDC_OP_GET: {
        NodeSnap s;
        nops_snap(n, &s);
        if (r->done != NULL) {
            uint32_t len = 0;
            uint64_t seq = 0;
            status = nops_result(&s, r->done->value, &len, &seq);
            r->done->len = len;
            r->done->seqno = seq;
        }
        break;
    }
    default:
        status = MT_ERR_ARG;
        break;
    }
    if (r->done != NULL)
        complete(r->done, status);
}

static inline void mark_done(SvConsumer *c, uint64_t *done)
{
    (*done)++;
    __atomic_store_n(&c->done, *done, __ATOMIC_RELEASE);
}

/* Shared MPMC: pass a request to its owner's inbox; while that inbox is full,
 * keep draining our own inbox so two forwarding consumers cannot deadlock. */
static void forward(SvConsumer *c, unsigned owner, const VdcRequest *r, uint64_t *done)
{
    VdcQueue *q = &c->sv->cons[owner].inbox;
    VdcRequest tmp;
    while (vq_push(q, r) != 0) {
        if (vq_pop_sc(&c->inbox, &tmp) == 0) {
            apply(c->sv, &tmp);
            mark_done(c, done);
        } else {
            vdc_cpu_relax();
        }
    }
    c->forwarded++;
}

static void *consumer_main(void *arg)
{
    SvConsumer *c = (SvConsumer *)arg;
    VdcService *sv = c->sv;
    uint64_t done = 0;
    VdcRequest r;

    if (c->cpu >= 0)
        (void)vdc_pin_thread(c->cpu);

    while (!__atomic_load_n(&sv->stop, __ATOMIC_ACQUIRE)) {
        int got = 0;

        /* Own inbox: always only keys this consumer owns. */
        for (int i = 0; i < 64 && vq_pop_sc(&c->inbox, &r) == 0; i++) {
            apply(sv, &r);
            mark_done(c, &done);
            got = 1;
        }

        if (sv->has_shared) {
            for (int i = 0; i < 64 && vq_pop_mc(&sv->shared, &r) == 0; i++) {
                const unsigned owner = sv_owner(sv, r.key);
                if (owner == c->id)
                    apply(sv, &r);
                else
                    forward(c, owner, &r, &done);
                mark_done(c, &done);
                got = 1;
            }
        }
        if (!got)
            vdc_cpu_relax();
    }
    return NULL;
}

static void sv_free(VdcService *sv)
{
    if (sv->cons != NULL) {
        for (unsigned i = 0; i < sv->ncons; i++)
            vq_destroy(&sv->cons[i].inbox);
        free(sv->cons);
    }
    if (sv->has_shared)
        vq_destroy(&sv->shared);
    vdc_space_destroy(&sv->space);
    free(sv);
}

VdcService *sv_create(const SvConfig *cfg)
{
    if (cfg == NULL || cfg->mode < 0 || cfg->mode >= SV_MODE_COUNT || cfg->j > VDC_KEY_BITS)
        return NULL;
    const unsigned ncons = cfg->mode == SV_MPSC ? 1u : cfg->consumers;
    if (ncons == 0 || ncons > 1024)
        return NULL;

    VdcService *sv = aligned_alloc(64, sizeof(VdcService));
    if (sv == NULL)
        return NULL;
    memset(sv, 0, sizeof(*sv));
    sv->cfg = *cfg;
    sv->cfg.consumer_cpus = NULL; /* the caller's array may not outlive this call */
    sv->ncons = ncons;

    if (vdc_space_init_ex(&sv->space, cfg->j, cfg->page_mode, 1) != 0) {
        free(sv);
        return NULL;
    }
    if (cfg->mode == SV_SHARED_MPMC) {
        if (vq_init(&sv->shared, cfg->queue_capacity) != 0) {
            vdc_space_destroy(&sv->space);
            free(sv);
            return NULL;
        }
        sv->has_shared = 1;
    }

    sv->cons = aligned_alloc(64, (size_t)ncons * sizeof(SvConsumer));
    if (sv->cons == NULL) {
        sv_free(sv);
        return NULL;
    }
    memset(sv->cons, 0, (size_t)ncons * sizeof(SvConsumer));
    for (unsigned i = 0; i < ncons; i++) {
        SvConsumer *c = &sv->cons[i];
        c->sv = sv;
        c->id = i;
        c->cpu = cfg->consumer_cpus != NULL ? cfg->consumer_cpus[i] : -1;
        if (vq_init(&c->inbox, cfg->queue_capacity) != 0) {
            sv_free(sv);
            return NULL;
        }
    }
    for (unsigned i = 0; i < ncons; i++) {
        if (pthread_create(&sv->cons[i].thread, NULL, consumer_main, &sv->cons[i]) != 0) {
            sv_destroy(sv);
            return NULL;
        }
        sv->cons[i].started = 1;
    }
    return sv;
}

void sv_destroy(VdcService *sv)
{
    if (sv == NULL)
        return;
    __atomic_store_n(&sv->stop, 1, __ATOMIC_RELEASE);
    for (unsigned i = 0; i < sv->ncons; i++)
        if (sv->cons[i].started)
            pthread_join(sv->cons[i].thread, NULL);
    sv_free(sv);
}

int sv_submit(VdcService *sv, const VdcRequest *r)
{
    int err = MT_OK;
    if (r->key >= VDC_NUM_SLOTS)
        err = MT_ERR_KEY_RANGE;
    else if (r->len > VDC_NODE_VALUE_BYTES || r->op < VDC_OP_PUT || r->op > VDC_OP_DELETE)
        err = MT_ERR_ARG;
    if (err != MT_OK) {
        if (r->done != NULL)
            complete(r->done, err);
        return err;
    }

    VdcQueue *q = sv->has_shared ? &sv->shared : &sv->cons[sv_owner(sv, r->key)].inbox;
    while (vq_push(q, r) != 0)
        vdc_cpu_relax(); /* queue full: back-pressure */
    return MT_OK;
}

/* Submit one request and spin until its result is back. */
static int sv_call(VdcService *sv, VdcRequest *r, void *out, uint32_t *len, uint64_t *seqno)
{
    VdcCompletion c;
    memset(&c, 0, sizeof(c));
    r->done = &c;
    const int rc = sv_submit(sv, r);
    if (rc != MT_OK)
        return rc;
    while (!__atomic_load_n(&c.done, __ATOMIC_ACQUIRE))
        vdc_cpu_relax();
    if (c.status == MT_OK && out != NULL && c.len > 0)
        memcpy(out, c.value, c.len);
    if (len != NULL)
        *len = c.len;
    if (seqno != NULL)
        *seqno = c.seqno;
    return c.status;
}

int sv_put(VdcService *sv, uint32_t key, const void *value, size_t len, uint64_t seqno)
{
    if (len > VDC_NODE_VALUE_BYTES || (len > 0 && value == NULL))
        return MT_ERR_ARG;
    VdcRequest r;
    memset(&r, 0, sizeof(r));
    r.op = VDC_OP_PUT;
    r.key = key;
    r.len = (uint8_t)len;
    r.seqno = seqno;
    if (len > 0)
        memcpy(r.value, value, len);
    return sv_call(sv, &r, NULL, NULL, NULL);
}

int sv_delete(VdcService *sv, uint32_t key, uint64_t seqno)
{
    VdcRequest r;
    memset(&r, 0, sizeof(r));
    r.op = VDC_OP_DELETE;
    r.key = key;
    r.seqno = seqno;
    return sv_call(sv, &r, NULL, NULL, NULL);
}

int sv_get(VdcService *sv, uint32_t key, void *out, uint32_t *len, uint64_t *seqno)
{
    VdcRequest r;
    memset(&r, 0, sizeof(r));
    r.op = VDC_OP_GET;
    r.key = key;
    return sv_call(sv, &r, out, len, seqno);
}

void sv_quiesce(VdcService *sv)
{
    for (;;) {
        /* Read "done" before "enqueued": every done request was enqueued earlier,
         * so equality means nothing is left. */
        uint64_t d = 0, e = 0;
        for (unsigned i = 0; i < sv->ncons; i++)
            d += __atomic_load_n(&sv->cons[i].done, __ATOMIC_ACQUIRE);
        for (unsigned i = 0; i < sv->ncons; i++)
            e += __atomic_load_n(&sv->cons[i].inbox.enqueue_pos, __ATOMIC_ACQUIRE);
        if (sv->has_shared)
            e += __atomic_load_n(&sv->shared.enqueue_pos, __ATOMIC_ACQUIRE);
        if (d == e)
            return;
        vdc_cpu_relax();
    }
}

void sv_clear(VdcService *sv)
{
    sv_quiesce(sv);
    vdc_space_clear(&sv->space); /* consumers are idle: they only touch Nodes while applying */
}

uint32_t sv_count(VdcService *sv, uint32_t *tombstones)
{
    uint32_t n = 0, t = 0;
    for (uint32_t i = 0; i < VDC_NUM_SLOTS; i++) {
        const uint32_t f = __atomic_load_n(&sv->space.slots[i].flags, __ATOMIC_ACQUIRE);
        n += (f & NODE_F_OCCUPIED) != 0;
        t += (f & NODE_F_TOMBSTONE) != 0;
    }
    if (tombstones != NULL)
        *tombstones = t;
    return n;
}

uint64_t sv_forwarded(VdcService *sv)
{
    uint64_t f = 0;
    for (unsigned i = 0; i < sv->ncons; i++)
        f += __atomic_load_n(&sv->cons[i].forwarded, __ATOMIC_RELAXED);
    return f;
}
