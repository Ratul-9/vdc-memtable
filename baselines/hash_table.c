/* Open-addressing hash table with linear probing (baseline). */
#include <string.h>

#include "baselines.h"

/* MurmurHash3 finalizer: good avalanche for integer keys. */
static inline uint32_t ht_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return x;
}

static inline size_t ht_bytes(const HashTable *ht)
{
    return ((size_t)ht->mask + 1) * sizeof(HtEntry);
}

int ht_init(HashTable *ht, unsigned log2_capacity, VdcPageMode mode, int populate)
{
    if (ht == NULL)
        return MT_ERR_ARG;
    memset(ht, 0, sizeof(*ht));
    if (log2_capacity < 1 || log2_capacity > 28)
        return MT_ERR_ARG;

    const size_t cap = (size_t)1 << log2_capacity;
    void *p = vdc_map(cap * sizeof(HtEntry), mode, populate);
    if (p == NULL)
        return MT_ERR_ALLOC;

    ht->entries = (HtEntry *)p;
    ht->mask = (uint32_t)(cap - 1);
    ht->page_mode = mode;
    return MT_OK;
}

void ht_destroy(HashTable *ht)
{
    if (ht == NULL || ht->entries == NULL)
        return;
    vdc_unmap(ht->entries, ht_bytes(ht), ht->page_mode);
    memset(ht, 0, sizeof(*ht));
}

void ht_clear(HashTable *ht)
{
    if (ht == NULL || ht->entries == NULL)
        return;
    memset(ht->entries, 0, ht_bytes(ht));
    ht->num_entries = 0;
}

/* Entry holding key, or the empty entry where it would go; NULL if the table is full. */
static HtEntry *ht_find(const HashTable *ht, uint32_t key)
{
    uint32_t i = ht_hash(key) & ht->mask;
    for (uint32_t probes = 0; probes <= ht->mask; probes++) {
        HtEntry *e = &ht->entries[i];
        if (!(e->flags & BL_F_OCCUPIED) || e->key == key)
            return e;
        i = (i + 1) & ht->mask;
    }
    return NULL;
}

int ht_put(HashTable *ht, uint32_t key, const void *value, size_t len)
{
    if (len > BL_VALUE_BYTES || (len > 0 && value == NULL))
        return MT_ERR_ARG;
    HtEntry *e = ht_find(ht, key);
    if (e == NULL)
        return MT_ERR_ARENA_FULL;
    if (!(e->flags & BL_F_OCCUPIED))
        ht->num_entries++;
    e->key = key;
    e->val_len = (uint32_t)len;
    if (len > 0)
        memcpy(e->value, value, len);
    e->flags = BL_F_OCCUPIED;
    return MT_OK;
}

int ht_get(const HashTable *ht, uint32_t key, const void **value, uint32_t *len)
{
    const HtEntry *e = ht_find(ht, key);
    if (e == NULL || !(e->flags & BL_F_OCCUPIED))
        return MT_NOT_FOUND;
    if (e->flags & BL_F_TOMBSTONE)
        return MT_DELETED;
    if (value != NULL)
        *value = e->value;
    if (len != NULL)
        *len = e->val_len;
    return MT_OK;
}

int ht_delete(HashTable *ht, uint32_t key)
{
    HtEntry *e = ht_find(ht, key);
    if (e == NULL)
        return MT_ERR_ARENA_FULL;
    if (!(e->flags & BL_F_OCCUPIED))
        ht->num_entries++;
    e->key = key;
    e->val_len = 0;
    e->flags = BL_F_OCCUPIED | BL_F_TOMBSTONE;
    return MT_OK;
}
