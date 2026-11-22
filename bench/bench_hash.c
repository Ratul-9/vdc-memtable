/* Benchmark adapter for the linear-probing hash table baseline. */
#include <errno.h>
#include <stdlib.h>

#include "baselines.h"
#include "bench.h"

#define IMPL_T HashTable
#define IMPL_PREFIX hash
#define IMPL_PUT(t, k, v, l) ht_put((t), (k), (v), (l))
#define IMPL_GET(t, k, v, l) ht_get((t), (k), (v), (l))
#define IMPL_DEL(t, k) ht_delete((t), (k))
#include "bench_phases.h"

static void *hash_create(const BenchImplConfig *cfg)
{
    HashTable *ht = malloc(sizeof(*ht));
    if (ht == NULL)
        return NULL;
    /* Capacity >= 2 x keys: load factor <= 0.5. */
    unsigned lg = 10;
    while (((size_t)1 << lg) < 2 * cfg->max_keys)
        lg++;
    if (ht_init(ht, lg, cfg->page_mode, 1) != MT_OK) {
        const int e = errno;
        free(ht);
        errno = e;
        return NULL;
    }
    return ht;
}

static void hash_destroy(void *t)
{
    ht_destroy((HashTable *)t);
    free(t);
}

static void hash_clear(void *t) { ht_clear((HashTable *)t); }

const BenchImpl bench_impl_hash = {"hash", 0, 0, hash_create, hash_destroy, hash_clear, hash_run_phase};
