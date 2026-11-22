/* Memtable: put / get / delete semantics, statistics, errors, random model check. */
#include <stdlib.h>
#include <string.h>

#include "tests.h"
#include "vdc/memtable.h"

static unsigned char model_state[VDC_NUM_SLOTS]; /* 0 never written, 1 live, 2 deleted */
static unsigned char model_value[VDC_NUM_SLOTS];

static int test_basic(void)
{
    Memtable mt;
    const void *v;
    uint32_t l;
    char big[100];
    memset(big, 'B', sizeof(big));

    CHECK(memtable_init(&mt, 6, 1u << 20) == MT_OK);
    CHECK(memtable_memory_bytes(&mt) == VDC_SPACE_BYTES + mt.arena.capacity);
    CHECK(memtable_capacity(&mt) == VDC_NUM_SLOTS);
    CHECK(memtable_is_empty(&mt) && !memtable_is_full(&mt));
    CHECK(memtable_get(&mt, 5, &v, &l) == MT_NOT_FOUND);

    /* Inline value, overwrite. */
    CHECK(memtable_put(&mt, 5, "abc", 3) == MT_OK);
    CHECK(memtable_get(&mt, 5, &v, &l) == MT_OK && l == 3 && memcmp(v, "abc", 3) == 0);
    CHECK(memtable_count(&mt) == 1 && memtable_value_bytes(&mt) == 3);
    CHECK(memtable_put(&mt, 5, "xy", 2) == MT_OK);
    CHECK(memtable_count(&mt) == 1 && memtable_value_bytes(&mt) == 2);
    CHECK(memtable_seqno(&mt) == 2);

    /* External value, then a shorter one reuses the arena bytes. */
    CHECK(memtable_put(&mt, 5, big, 100) == MT_OK);
    CHECK(memtable_get(&mt, 5, &v, &l) == MT_OK && l == 100 && memcmp(v, big, 100) == 0);
    const size_t used = memtable_arena_used(&mt);
    CHECK(used >= 100);
    CHECK(memtable_put(&mt, 5, big, 60) == MT_OK);
    CHECK(memtable_arena_used(&mt) == used);
    CHECK(memtable_get(&mt, 5, &v, &l) == MT_OK && l == 60);
    CHECK(memtable_value_bytes(&mt) == 60);

    /* Delete = tombstone. */
    CHECK(memtable_delete(&mt, 5) == MT_OK);
    CHECK(memtable_get(&mt, 5, &v, &l) == MT_DELETED);
    CHECK(memtable_count(&mt) == 0 && memtable_tombstones(&mt) == 1);
    CHECK(memtable_entries(&mt) == 1 && memtable_value_bytes(&mt) == 0);
    CHECK(memtable_delete(&mt, 5) == MT_OK); /* deleting twice changes no counts */
    CHECK(memtable_tombstones(&mt) == 1 && memtable_entries(&mt) == 1);

    /* Deleting a never-written key still writes a tombstone. */
    CHECK(memtable_delete(&mt, 9) == MT_OK);
    CHECK(memtable_get(&mt, 9, &v, &l) == MT_DELETED);
    CHECK(memtable_tombstones(&mt) == 2 && memtable_entries(&mt) == 2);

    /* A put after a delete revives the key. */
    CHECK(memtable_put(&mt, 5, "z", 1) == MT_OK);
    CHECK(memtable_get(&mt, 5, &v, &l) == MT_OK && l == 1);
    CHECK(memtable_count(&mt) == 1 && memtable_tombstones(&mt) == 1);

    /* Errors. */
    CHECK(memtable_put(&mt, VDC_NUM_SLOTS, "z", 1) == MT_ERR_KEY_RANGE);
    CHECK(memtable_delete(&mt, VDC_NUM_SLOTS) == MT_ERR_KEY_RANGE);
    CHECK(memtable_get(&mt, VDC_NUM_SLOTS, &v, &l) == MT_ERR_KEY_RANGE);
    CHECK(memtable_put(&mt, 7, NULL, 3) == MT_ERR_ARG);
    CHECK(memtable_put(&mt, 7, NULL, 0) == MT_OK); /* empty value is allowed */
    CHECK(memtable_get(&mt, 7, &v, &l) == MT_OK && l == 0);
    CHECK(memtable_get(&mt, 7, NULL, NULL) == MT_OK);

    memtable_destroy(&mt);
    memtable_destroy(&mt); /* double destroy is safe */
    return 0;
}

static int test_no_arena(void)
{
    Memtable mt;
    const void *v;
    uint32_t l;
    char big[100];
    memset(big, 'B', sizeof(big));

    CHECK(memtable_init(&mt, 0, 0) == MT_OK);
    /* Too large for inline and no arena: rejected, memtable unchanged. */
    CHECK(memtable_put(&mt, 1, big, 100) == MT_ERR_ARENA_FULL);
    CHECK(memtable_entries(&mt) == 0 && memtable_seqno(&mt) == 0);
    CHECK(memtable_get(&mt, 1, &v, &l) == MT_NOT_FOUND);
    CHECK(memtable_put(&mt, 1, big, 32) == MT_OK); /* 32 B fits inline */
    CHECK(memtable_put(&mt, 1, big, 33) == MT_ERR_ARENA_FULL);
    CHECK(memtable_get(&mt, 1, &v, &l) == MT_OK && l == 32); /* old value intact */
    memtable_destroy(&mt);

    CHECK(memtable_init(NULL, 0, 0) == MT_ERR_ARG);
    CHECK(memtable_init(&mt, VDC_KEY_BITS + 1, 0) == MT_ERR_ARG);
    return 0;
}

static int test_random_model(void)
{
    Memtable mt;
    const void *v;
    uint32_t l;

    memset(model_state, 0, sizeof(model_state));
    CHECK(memtable_init(&mt, 6, 0) == MT_OK);

    uint32_t live = 0, tomb = 0;
    srand(1);
    for (int i = 0; i < 2000000; i++) {
        const uint32_t k = ((uint32_t)rand() * 7919u) % VDC_NUM_SLOTS;
        if (rand() % 3) {
            const unsigned char b = (unsigned char)rand();
            CHECK(memtable_put(&mt, k, &b, 1) == MT_OK);
            if (model_state[k] == 0) {
                live++;
            } else if (model_state[k] == 2) {
                tomb--;
                live++;
            }
            model_state[k] = 1;
            model_value[k] = b;
        } else {
            CHECK(memtable_delete(&mt, k) == MT_OK);
            if (model_state[k] == 0) {
                tomb++;
            } else if (model_state[k] == 1) {
                live--;
                tomb++;
            }
            model_state[k] = 2;
        }
    }

    CHECK(memtable_count(&mt) == live);
    CHECK(memtable_tombstones(&mt) == tomb);
    CHECK(memtable_entries(&mt) == live + tomb);

    for (uint32_t k = 0; k < VDC_NUM_SLOTS; k++) {
        const int r = memtable_get(&mt, k, &v, &l);
        if (model_state[k] == 0) {
            CHECK(r == MT_NOT_FOUND);
        } else if (model_state[k] == 2) {
            CHECK(r == MT_DELETED);
        } else {
            CHECK(r == MT_OK && l == 1 && *(const unsigned char *)v == model_value[k]);
        }
    }
    memtable_destroy(&mt);
    return 0;
}

enum { BATCH_N = 1000 };
static uint32_t batch_keys[BATCH_N + 2];
static const void *batch_vals[BATCH_N];
static uint32_t batch_lens[BATCH_N];
static uint32_t batch_payload[BATCH_N];
static const void *out_vals[BATCH_N + 2];
static uint32_t out_lens[BATCH_N + 2];
static int out_status[BATCH_N + 2];

/* Config init, put_batch / get_batch, clear. */
static int test_config_batch_clear(void)
{
    Memtable mt;
    MemtableConfig cfg = memtable_default_config();
    CHECK(cfg.j == VDC_DEFAULT_J && cfg.page_mode == VDC_PAGES_BASE);
    cfg.j = 11;
    cfg.arena_bytes = 0;
    cfg.populate = 1;
    CHECK(memtable_init_config(&mt, &cfg) == MT_OK);
    CHECK(mt.space.j == 11 && mt.arena.capacity == 0);

    /* 7919 is odd, so k * 7919 mod 2^20 is a bijection: keys are distinct. */
    for (uint32_t i = 0; i < BATCH_N; i++) {
        batch_keys[i] = (i * 7919u) % VDC_NUM_SLOTS;
        batch_payload[i] = i;
        batch_vals[i] = &batch_payload[i];
        batch_lens[i] = sizeof(uint32_t);
    }
    size_t applied = 0;
    CHECK(memtable_put_batch(&mt, batch_keys, batch_vals, batch_lens, BATCH_N, &applied) == MT_OK);
    CHECK(applied == BATCH_N && memtable_count(&mt) == BATCH_N);

    /* One missing key and one out-of-range key at the end. */
    batch_keys[BATCH_N] = (BATCH_N * 7919u) % VDC_NUM_SLOTS;
    batch_keys[BATCH_N + 1] = VDC_NUM_SLOTS;
    const size_t found = memtable_get_batch(&mt, batch_keys, BATCH_N + 2, out_vals, out_lens, out_status);
    CHECK(found == BATCH_N);
    for (uint32_t i = 0; i < BATCH_N; i++) {
        uint32_t v;
        CHECK(out_status[i] == MT_OK && out_lens[i] == sizeof(uint32_t));
        memcpy(&v, out_vals[i], sizeof(v));
        CHECK(v == i);
    }
    CHECK(out_status[BATCH_N] == MT_NOT_FOUND && out_vals[BATCH_N] == NULL && out_lens[BATCH_N] == 0);
    CHECK(out_status[BATCH_N + 1] == MT_ERR_KEY_RANGE);
    CHECK(memtable_get_batch(&mt, batch_keys, BATCH_N, NULL, NULL, NULL) == BATCH_N);

    /* put_batch stops at the first error. */
    batch_keys[3] = VDC_NUM_SLOTS;
    CHECK(memtable_put_batch(&mt, batch_keys, batch_vals, batch_lens, 10, &applied) == MT_ERR_KEY_RANGE);
    CHECK(applied == 3);

    /* Clear empties the table and keeps it usable. */
    memtable_clear(&mt);
    CHECK(memtable_is_empty(&mt) && memtable_seqno(&mt) == 0 && memtable_value_bytes(&mt) == 0);
    CHECK(memtable_get(&mt, batch_keys[0], NULL, NULL) == MT_NOT_FOUND);
    CHECK(memtable_put(&mt, 1, "x", 1) == MT_OK && memtable_count(&mt) == 1);
    memtable_destroy(&mt);

    /* Bad configs. */
    CHECK(memtable_init_config(&mt, NULL) == MT_ERR_ARG);
    cfg.j = VDC_KEY_BITS + 1;
    CHECK(memtable_init_config(&mt, &cfg) == MT_ERR_ARG);
    return 0;
}

int test_memtable(void)
{
    CHECK(test_basic() == 0);
    CHECK(test_no_arena() == 0);
    CHECK(test_random_model() == 0);
    CHECK(test_config_batch_clear() == 0);
    return 0;
}
