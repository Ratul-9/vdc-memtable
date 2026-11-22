/* Benchmark adapter for the VDC memtable. */
#include <errno.h>
#include <stdlib.h>

#include "bench.h"
#include "vdc/memtable.h"

static const void *bp_values[BENCH_BATCH_CHUNK];
static uint32_t bp_lens[BENCH_BATCH_CHUNK];
static int bp_status[BENCH_BATCH_CHUNK];

#define IMPL_T Memtable
#define IMPL_PREFIX vdc
#define IMPL_PUT(t, k, v, l) memtable_put((t), (k), (v), (l))
#define IMPL_GET(t, k, v, l) memtable_get((t), (k), (v), (l))
#define IMPL_DEL(t, k) memtable_delete((t), (k))
#define IMPL_GET_BATCH(t, keys, n, v, l, s) memtable_get_batch((t), (keys), (n), (v), (l), (s))
#include "bench_phases.h"

static void *vdc_create(const BenchImplConfig *cfg)
{
    Memtable *mt = malloc(sizeof(*mt));
    if (mt == NULL)
        return NULL;
    MemtableConfig c = memtable_default_config();
    c.j = cfg->j;
    c.arena_bytes = 0; /* inline 32 B values only */
    c.page_mode = cfg->page_mode;
    c.populate = 1;    /* no page faults during measurements */
    if (memtable_init_config(mt, &c) != MT_OK) {
        const int e = errno;
        free(mt);
        errno = e;
        return NULL;
    }
    return mt;
}

static void vdc_destroy(void *t)
{
    memtable_destroy((Memtable *)t);
    free(t);
}

static void vdc_clear(void *t) { memtable_clear((Memtable *)t); }

const BenchImpl bench_impl_vdc = {"vdc", 1, 1, vdc_create, vdc_destroy, vdc_clear, vdc_run_phase};
