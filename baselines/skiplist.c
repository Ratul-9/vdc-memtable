/* Skip list (LevelDB/RocksDB style, p = 1/4) with nodes in a bump arena (baseline). */
#include <string.h>

#include "baselines.h"

static inline uint64_t sl_rand(SkipList *sl)
{
    uint64_t x = sl->rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    sl->rng = x;
    return x;
}

static uint32_t sl_random_height(SkipList *sl)
{
    uint32_t h = 1;
    while (h < SL_MAX_HEIGHT && (sl_rand(sl) & 3u) == 0)
        h++;
    return h;
}

static SlNode *sl_alloc(SkipList *sl, uint32_t height)
{
    size_t sz = offsetof(SlNode, next) + (size_t)height * sizeof(SlNode *);
    sz = (sz + 7u) & ~(size_t)7u;
    if (sz > sl->arena_bytes - sl->arena_used)
        return NULL;
    SlNode *n = (SlNode *)(void *)(sl->arena + sl->arena_used);
    sl->arena_used += sz;
    memset(n, 0, sz);
    n->height = height;
    return n;
}

static void sl_reset(SkipList *sl)
{
    sl->arena_used = 0;
    sl->height = 1;
    sl->num_entries = 0;
    sl->rng = 0x9E3779B97F4A7C15ull;
    sl->head = sl_alloc(sl, SL_MAX_HEIGHT);
}

int sl_init(SkipList *sl, size_t arena_bytes, VdcPageMode mode, int populate)
{
    if (sl == NULL)
        return MT_ERR_ARG;
    memset(sl, 0, sizeof(*sl));
    if (arena_bytes < 4096)
        return MT_ERR_ARG;

    const size_t len = vdc_map_size(arena_bytes, mode);
    if (len == 0)
        return MT_ERR_ARG;
    void *p = vdc_map(len, mode, populate);
    if (p == NULL)
        return MT_ERR_ALLOC;

    sl->arena = (uint8_t *)p;
    sl->arena_bytes = len;
    sl->page_mode = mode;
    sl_reset(sl);
    return MT_OK;
}

void sl_destroy(SkipList *sl)
{
    if (sl == NULL || sl->arena == NULL)
        return;
    vdc_unmap(sl->arena, sl->arena_bytes, sl->page_mode);
    memset(sl, 0, sizeof(*sl));
}

void sl_clear(SkipList *sl)
{
    if (sl != NULL && sl->arena != NULL)
        sl_reset(sl);
}

/* First node with node->key >= key. If prev != NULL, prev[l] = last node before it at level l. */
static SlNode *sl_find_ge(const SkipList *sl, uint32_t key, SlNode **prev)
{
    SlNode *x = sl->head;
    for (int lvl = (int)sl->height - 1; lvl >= 0; lvl--) {
        SlNode *nx;
        while ((nx = x->next[lvl]) != NULL && nx->key < key)
            x = nx;
        if (prev != NULL)
            prev[lvl] = x;
    }
    return x->next[0];
}

static int sl_write(SkipList *sl, uint32_t key, const void *value, size_t len, uint32_t flags)
{
    SlNode *prev[SL_MAX_HEIGHT];
    SlNode *n = sl_find_ge(sl, key, prev);

    if (n == NULL || n->key != key) {
        const uint32_t h = sl_random_height(sl);
        n = sl_alloc(sl, h);
        if (n == NULL)
            return MT_ERR_ARENA_FULL;
        if (h > sl->height) {
            for (uint32_t l = sl->height; l < h; l++)
                prev[l] = sl->head;
            sl->height = h;
        }
        n->key = key;
        for (uint32_t l = 0; l < h; l++) {
            n->next[l] = prev[l]->next[l];
            prev[l]->next[l] = n;
        }
        sl->num_entries++;
    }

    if (len > 0)
        memcpy(n->value, value, len);
    n->val_len = (uint32_t)len;
    n->flags = flags;
    return MT_OK;
}

int sl_put(SkipList *sl, uint32_t key, const void *value, size_t len)
{
    if (len > BL_VALUE_BYTES || (len > 0 && value == NULL))
        return MT_ERR_ARG;
    return sl_write(sl, key, value, len, BL_F_OCCUPIED);
}

int sl_delete(SkipList *sl, uint32_t key)
{
    return sl_write(sl, key, NULL, 0, BL_F_OCCUPIED | BL_F_TOMBSTONE);
}

int sl_get(const SkipList *sl, uint32_t key, const void **value, uint32_t *len)
{
    const SlNode *n = sl_find_ge(sl, key, NULL);
    if (n == NULL || n->key != key)
        return MT_NOT_FOUND;
    if (n->flags & BL_F_TOMBSTONE)
        return MT_DELETED;
    if (value != NULL)
        *value = n->value;
    if (len != NULL)
        *len = n->val_len;
    return MT_OK;
}

size_t sl_scan(const SkipList *sl, uint32_t start, uint32_t end, size_t limit, uint32_t *keys,
               const void **values)
{
    size_t got = 0;
    for (const SlNode *n = sl_find_ge(sl, start, NULL); n != NULL && n->key < end && got < limit; n = n->next[0]) {
        if (n->flags & BL_F_TOMBSTONE)
            continue;
        if (keys != NULL)
            keys[got] = n->key;
        if (values != NULL)
            values[got] = n->value;
        got++;
    }
    return got;
}
