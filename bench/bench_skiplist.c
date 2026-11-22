/* Benchmark adapter for the skip list baseline. */
#include <errno.h>
#include <stdlib.h>

#include "baselines.h"
#include "bench.h"

#define IMPL_T SkipList
#define IMPL_PREFIX skiplist
#define IMPL_PUT(t, k, v, l) sl_put((t), (k), (v), (l))
#define IMPL_GET(t, k, v, l) sl_get((t), (k), (v), (l))
#define IMPL_DEL(t, k) sl_delete((t), (k))
#include "bench_phases.h"

static void *skiplist_create(const BenchImplConfig *cfg)
{
    SkipList *sl = malloc(sizeof(*sl));
    if (sl == NULL)
        return NULL;
    /* About 60 B per node on average; 96 B per key leaves headroom. */
    const size_t arena = cfg->max_keys * 96u + 65536u;
    if (sl_init(sl, arena, cfg->page_mode, 1) != MT_OK) {
        const int e = errno;
        free(sl);
        errno = e;
        return NULL;
    }
    return sl;
}

static void skiplist_destroy(void *t)
{
    sl_destroy((SkipList *)t);
    free(t);
}

static void skiplist_clear(void *t) { sl_clear((SkipList *)t); }

const BenchImpl bench_impl_skiplist = {"skiplist", 0, 0, skiplist_create, skiplist_destroy, skiplist_clear,
                                       skiplist_run_phase};
