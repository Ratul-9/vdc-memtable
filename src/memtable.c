#include "vdc/memtable.h"

#include <string.h>

int memtable_init_config(Memtable *mt, const MemtableConfig *cfg)
{
    if (mt == NULL || cfg == NULL || cfg->j > VDC_KEY_BITS)
        return MT_ERR_ARG;

    memset(mt, 0, sizeof(*mt));

    if (vdc_space_init_ex(&mt->space, cfg->j, cfg->page_mode, cfg->populate) != 0)
        return MT_ERR_ALLOC;

    if (cfg->arena_bytes > 0 &&
        arena_init_ex(&mt->arena, cfg->arena_bytes, cfg->page_mode, cfg->populate) != 0) {
        vdc_space_destroy(&mt->space);
        memset(mt, 0, sizeof(*mt));
        return MT_ERR_ALLOC;
    }
    return MT_OK;
}

int memtable_init(Memtable *mt, unsigned j, size_t arena_bytes)
{
    MemtableConfig cfg = memtable_default_config();
    cfg.j = j;
    cfg.arena_bytes = arena_bytes;
    return memtable_init_config(mt, &cfg);
}

void memtable_destroy(Memtable *mt)
{
    if (mt == NULL)
        return;
    arena_destroy(&mt->arena);
    vdc_space_destroy(&mt->space);
    memset(mt, 0, sizeof(*mt));
}

void memtable_clear(Memtable *mt)
{
    if (mt == NULL)
        return;
    vdc_space_clear(&mt->space);
    arena_reset(&mt->arena);
    mt->seqno = 0;
    mt->num_entries = 0;
    mt->num_tombstones = 0;
    mt->value_bytes = 0;
}

int memtable_put(Memtable *mt, uint32_t key, const void *value, size_t len)
{
    if (key >= VDC_NUM_SLOTS)
        return MT_ERR_KEY_RANGE;
    if ((len > 0 && value == NULL) || len > UINT32_MAX)
        return MT_ERR_ARG;

    Node *n = vdc_space_node(&mt->space, key);
    const uint32_t old_flags = n->flags;
    const int occupied = (old_flags & NODE_F_OCCUPIED) != 0;
    const int tombstone = (old_flags & NODE_F_TOMBSTONE) != 0;
    const int live = occupied && !tombstone;

    /* Decide where the value goes. Nothing is modified until this succeeds. */
    uint64_t ext_off = 0;
    uint32_t flags = NODE_F_OCCUPIED;

    if (len > VDC_NODE_VALUE_BYTES) {
        if (live && (old_flags & NODE_F_EXTERNAL) && len <= n->val_len) {
            ext_off = n->ext_off; /* reuse the old arena bytes */
        } else {
            ext_off = arena_alloc(&mt->arena, len);
            if (ext_off == ARENA_FAIL)
                return MT_ERR_ARENA_FULL;
        }
        memcpy(arena_ptr(&mt->arena, ext_off), value, len);
        flags |= NODE_F_EXTERNAL;
    } else if (len > 0) {
        memcpy(n->value, value, len);
    }

    /* Statistics. */
    if (!occupied) {
        mt->num_entries++;
    } else if (tombstone) {
        mt->num_tombstones--;
    }
    if (live)
        mt->value_bytes -= n->val_len;
    mt->value_bytes += len;

    node_set_key(n, key);
    n->seqno = ++mt->seqno;
    n->ext_off = ext_off;
    n->val_len = (uint32_t)len;
    n->flags = flags;
    return MT_OK;
}

int memtable_delete(Memtable *mt, uint32_t key)
{
    if (key >= VDC_NUM_SLOTS)
        return MT_ERR_KEY_RANGE;

    Node *n = vdc_space_node(&mt->space, key);
    const uint32_t old_flags = n->flags;
    const int occupied = (old_flags & NODE_F_OCCUPIED) != 0;
    const int tombstone = (old_flags & NODE_F_TOMBSTONE) != 0;

    if (!occupied) {
        mt->num_entries++;
        mt->num_tombstones++;
    } else if (!tombstone) {
        mt->value_bytes -= n->val_len;
        mt->num_tombstones++;
    }

    node_set_key(n, key);
    n->seqno = ++mt->seqno;
    n->ext_off = 0;
    n->val_len = 0;
    n->flags = NODE_F_OCCUPIED | NODE_F_TOMBSTONE;
    return MT_OK;
}

int memtable_get(const Memtable *mt, uint32_t key, const void **value, uint32_t *len)
{
    if (key >= VDC_NUM_SLOTS)
        return MT_ERR_KEY_RANGE;

    const Node *n = &mt->space.slots[vdc_space_slot_of(&mt->space, key)];

    if (!(n->flags & NODE_F_OCCUPIED))
        return MT_NOT_FOUND;
    if (n->flags & NODE_F_TOMBSTONE)
        return MT_DELETED;

    if (value != NULL)
        *value = (n->flags & NODE_F_EXTERNAL) ? (const void *)arena_ptr(&mt->arena, n->ext_off)
                                              : (const void *)n->value;
    if (len != NULL)
        *len = n->val_len;
    return MT_OK;
}

/* Address of the Node of key, for prefetching (key must be < 2^20). */
static inline const Node *node_addr(const Memtable *mt, uint32_t key)
{
    return &mt->space.slots[vdc_space_slot_of(&mt->space, key)];
}

size_t memtable_get_batch(const Memtable *mt, const uint32_t *keys, size_t n,
                          const void **values, uint32_t *lens, int *status)
{
    const size_t d = VDC_PREFETCH_DISTANCE;
    size_t found = 0;

    for (size_t i = 0; i < n && i < d; i++)
        if (keys[i] < VDC_NUM_SLOTS)
            vdc_prefetch_r(node_addr(mt, keys[i]));

    for (size_t i = 0; i < n; i++) {
        if (i + d < n && keys[i + d] < VDC_NUM_SLOTS)
            vdc_prefetch_r(node_addr(mt, keys[i + d]));

        const void *v = NULL;
        uint32_t l = 0;
        const int rc = memtable_get(mt, keys[i], &v, &l);
        if (status != NULL)
            status[i] = rc;
        if (values != NULL)
            values[i] = (rc == MT_OK) ? v : NULL;
        if (lens != NULL)
            lens[i] = (rc == MT_OK) ? l : 0;
        found += (rc == MT_OK);
    }
    return found;
}

int memtable_put_batch(Memtable *mt, const uint32_t *keys, const void *const *values,
                       const uint32_t *lens, size_t n, size_t *applied)
{
    const size_t d = VDC_PREFETCH_DISTANCE;

    for (size_t i = 0; i < n && i < d; i++)
        if (keys[i] < VDC_NUM_SLOTS)
            vdc_prefetch_w(node_addr(mt, keys[i]));

    for (size_t i = 0; i < n; i++) {
        if (i + d < n && keys[i + d] < VDC_NUM_SLOTS)
            vdc_prefetch_w(node_addr(mt, keys[i + d]));

        const int rc = memtable_put(mt, keys[i], values[i], lens[i]);
        if (rc != MT_OK) {
            if (applied != NULL)
                *applied = i;
            return rc;
        }
    }
    if (applied != NULL)
        *applied = n;
    return MT_OK;
}
