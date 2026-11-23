/* DAT (scale) library: permutation, key sets, flat array, two-level rows. Small key ranges only. */
#include <stdlib.h>
#include <string.h>

#include "dat/dat_flat.h"
#include "dat/dat_keys.h"
#include "dat/dat_rows.h"
#include "tests.h"

/* Every n tested must map 0..n-1 onto 0..n-1 exactly once. */
static int test_perm(void)
{
    static const uint64_t ns[] = {1, 2, 3, 7, 64, 1000, 65536, 65539, 1000003};
    for (size_t t = 0; t < sizeof(ns) / sizeof(ns[0]); t++) {
        const uint64_t n = ns[t];
        unsigned char *seen = calloc(n, 1);
        CHECK(seen != NULL);
        DatPerm p;
        dat_perm_init(&p, n, 42 + t);
        int ok = 1;
        for (uint64_t i = 0; i < n; i++) {
            const uint64_t y = dat_perm(&p, i);
            if (y >= n || seen[y])
                ok = 0;
            else
                seen[y] = 1;
        }
        free(seen);
        CHECK(ok);
    }
    /* Scrambles: a random order is not the identity. */
    DatPerm p;
    dat_perm_init(&p, 100000, 7);
    uint64_t same = 0;
    for (uint64_t i = 0; i < 100000; i++)
        same += (dat_perm(&p, i) == i);
    CHECK(same < 1000);
    return 0;
}

/* Keys of a set are all different and in range; miss keys are never in the set. */
static int test_keyset(DatPattern pat, uint64_t n, uint64_t domain)
{
    DatKeySet k;
    CHECK(dat_keys_init(&k, pat, n, domain, 100, 9) == 0);
    unsigned char *in = calloc(domain, 1);
    CHECK(in != NULL);
    int ok = 1;
    for (uint64_t i = 0; i < n; i++) {
        const uint32_t key = dat_key(&k, i);
        if (key >= domain || in[key])
            ok = 0;
        else
            in[key] = 1;
    }
    for (uint64_t i = 0; i < k.n_miss; i++) {
        const uint32_t key = dat_miss_key(&k, i);
        if (key >= domain || in[key] == 1)
            ok = 0;
        else
            in[key] = 2; /* also catches a miss key returned twice */
    }
    free(in);
    CHECK(ok);
    CHECK(k.n_miss > 0);
    if (pat == DAT_DENSE)
        CHECK(dat_key(&k, n - 1) == n - 1);
    return 0;
}

static int test_keys(void)
{
    const uint64_t domain = (uint64_t)1 << 20;
    for (int p = 0; p < DAT_PATTERN_COUNT; p++) {
        CHECK(test_keyset((DatPattern)p, 10000, domain) == 0);
        CHECK(test_keyset((DatPattern)p, 600000, domain) == 0); /* more than half the domain */
    }
    DatKeySet k;
    CHECK(dat_keys_init(&k, DAT_DENSE, domain + 1, domain, 100, 1) != 0);
    CHECK(dat_keys_init(&k, DAT_CLUSTERED, 10, domain, 0, 1) != 0);
    CHECK(strcmp(dat_pattern_name(DAT_SPARSE), "sparse") == 0);
    return 0;
}

static int test_flat(void)
{
    DatFlat f;
    uint32_t v[8] = {0}, out[8];
    uint32_t len = 0;
    CHECK(dat_flat_init(&f, 0, VDC_PAGES_BASE) == MT_ERR_ARG);
    CHECK(dat_flat_init(&f, DAT_DOMAIN + 1, VDC_PAGES_BASE) == MT_ERR_ARG);
    CHECK(dat_flat_init(&f, 1u << 16, VDC_PAGES_BASE) == MT_OK);
    CHECK(f.lazy == 1);
    for (uint32_t key = 0; key < (1u << 16); key += 3) {
        dat_flat_touch(&f, key);
        v[0] = key;
        CHECK(dat_flat_put(&f, key, v, sizeof(v), key) == MT_OK);
    }
    for (uint32_t key = 0; key < (1u << 16); key++) {
        const int rc = dat_flat_get(&f, key, out, &len);
        if (key % 3 == 0)
            CHECK(rc == MT_OK && len == sizeof(v) && out[0] == key);
        else
            CHECK(rc == MT_NOT_FOUND);
    }
    CHECK(dat_flat_put(&f, 1u << 16, v, 4, 0) == MT_ERR_KEY_RANGE);
    CHECK(dat_flat_put(&f, 1, v, 33, 0) == MT_ERR_ARG);
    dat_flat_reset(&f, 3);
    CHECK(dat_flat_get(&f, 3, out, NULL) == MT_NOT_FOUND);
    dat_flat_destroy(&f);
    dat_flat_destroy(&f);
    return 0;
}

static int test_rows(void)
{
    DatRows r;
    uint32_t v[8] = {0}, out[8];
    uint32_t len = 0;
    const unsigned domain_bits = 16, row_bits = 6; /* 1024 rows of 64 keys */

    CHECK(dat_rows_init(&r, 0, 6, 10, VDC_PAGES_BASE) == MT_ERR_ARG);
    CHECK(dat_rows_init(&r, 16, 17, 10, VDC_PAGES_BASE) == MT_ERR_ARG);
    CHECK(dat_rows_init(&r, domain_bits, row_bits, 3, VDC_PAGES_BASE) == MT_OK);
    CHECK(r.table_rows == 1024 && r.arena_rows == 3 && r.used_rows == 0);

    /* Keys in rows 0, 5 and 1023: three rows created, in that order. */
    static const uint32_t keys[] = {1, 63, 5 * 64 + 7, 1023 * 64 + 63, 2};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        v[0] = keys[i];
        CHECK(dat_rows_put(&r, keys[i], v, sizeof(v), i) == MT_OK);
    }
    CHECK(r.used_rows == 3);
    CHECK(dat_rows_used_bytes(&r) == 1024 * 4 + 3 * 64 * 64);
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        CHECK(dat_rows_get(&r, keys[i], out, &len) == MT_OK && out[0] == keys[i] && len == sizeof(v));
    CHECK(dat_rows_get(&r, 0, out, NULL) == MT_NOT_FOUND);           /* row exists, key absent */
    CHECK(dat_rows_get(&r, 9 * 64, out, NULL) == MT_NOT_FOUND);      /* row does not exist */
    CHECK(dat_rows_put(&r, 9 * 64, v, 4, 0) == MT_ERR_ARENA_FULL);   /* a 4th row does not fit */
    CHECK(dat_rows_get(&r, 9 * 64, out, NULL) == MT_NOT_FOUND);      /* nothing changed */
    CHECK(dat_rows_put(&r, 1u << 16, v, 4, 0) == MT_ERR_KEY_RANGE);

    /* Sorted order: walking rows in order gives keys in ascending order. */
    uint32_t prev = 0;
    int first = 1, sorted = 1;
    for (uint64_t row = 0; row < r.table_rows; row++) {
        const uint32_t e = r.table[row];
        if (e == 0)
            continue;
        for (uint32_t c = 0; c < 64; c++) {
            const uint32_t key = (uint32_t)(row << row_bits) | c;
            if (dat_rows_get(&r, key, out, NULL) == MT_OK) {
                if (!first && key <= prev)
                    sorted = 0;
                prev = key;
                first = 0;
            }
        }
    }
    CHECK(sorted);

    dat_rows_clear(&r);
    CHECK(r.used_rows == 0);
    CHECK(dat_rows_get(&r, 1, out, NULL) == MT_NOT_FOUND);
    CHECK(dat_rows_put(&r, 9 * 64, v, 4, 0) == MT_OK);
    dat_rows_destroy(&r);
    dat_rows_destroy(&r);
    return 0;
}

int test_dat(void)
{
    CHECK(test_perm() == 0);
    CHECK(test_keys() == 0);
    CHECK(test_flat() == 0);
    CHECK(test_rows() == 0);
    return 0;
}
