/*
 * dat_keys.h - key sets generated on the fly (no key arrays in memory).
 *
 * DatPerm is a reversible scramble of 0 .. n-1 (a permutation): every input
 * gives a different output, so "the i-th random key" can be computed when
 * needed instead of stored. It uses add / multiply-by-odd / xor-shift steps on
 * the smallest power-of-two range >= n, repeated until the result is < n
 * ("cycle walking", fewer than 2 rounds on average).
 *
 * Key set patterns (n keys out of a range of `domain` keys):
 *   DAT_DENSE      keys 0 .. n-1
 *   DAT_CLUSTERED  runs of run_len consecutive keys at random places
 *   DAT_SPARSE     n random keys spread over the whole range
 * dat_key(i)      = the i-th key of the set (i < n), all different
 * dat_miss_key(i) = the i-th key NOT in the set (i < n_miss), all different
 */
#ifndef DAT_KEYS_H
#define DAT_KEYS_H

#include <stdint.h>

typedef struct DatPerm {
    uint64_t n;
    uint64_t mask;
    uint64_t seed;
    uint64_t k1, k2;
    unsigned bits;
    unsigned shift;
} DatPerm;

/* Permutation of 0 .. n-1 (n >= 1). */
void dat_perm_init(DatPerm *p, uint64_t n, uint64_t seed);

static inline uint64_t dat_perm_round(const DatPerm *p, uint64_t x)
{
    x = (x + p->seed) & p->mask;
    x = (x * p->k1) & p->mask;
    x ^= x >> p->shift;
    x = (x * p->k2) & p->mask;
    x ^= x >> p->shift;
    return x;
}

static inline uint64_t dat_perm(const DatPerm *p, uint64_t x)
{
    uint64_t y = dat_perm_round(p, x);
    while (y >= p->n)
        y = dat_perm_round(p, y);
    return y;
}

typedef enum DatPattern {
    DAT_DENSE,
    DAT_CLUSTERED,
    DAT_SPARSE,
    DAT_PATTERN_COUNT
} DatPattern;

typedef struct DatKeySet {
    DatPattern pattern;
    uint64_t n;          /* keys in the set */
    uint64_t domain;     /* keys are < domain */
    uint64_t run_len;    /* clustered */
    uint64_t runs_total; /* clustered: domain / run_len */
    uint64_t runs_used;  /* clustered: ceil(n / run_len) */
    uint64_t n_miss;     /* keys available for get_miss */
    DatPerm main;        /* sparse: over the domain; clustered: over run slots */
    DatPerm miss;        /* dense: over n .. domain-1 */
} DatKeySet;

/* 0 on success, -1 if the set does not fit in the domain. */
int dat_keys_init(DatKeySet *k, DatPattern pattern, uint64_t n, uint64_t domain, uint64_t run_len, uint64_t seed);

static inline uint32_t dat_key(const DatKeySet *k, uint64_t i)
{
    switch (k->pattern) {
    case DAT_DENSE:
        return (uint32_t)i;
    case DAT_CLUSTERED:
        return (uint32_t)(dat_perm(&k->main, i / k->run_len) * k->run_len + i % k->run_len);
    default:
        return (uint32_t)dat_perm(&k->main, i);
    }
}

static inline uint32_t dat_miss_key(const DatKeySet *k, uint64_t i)
{
    switch (k->pattern) {
    case DAT_DENSE:
        return (uint32_t)(k->n + dat_perm(&k->miss, i));
    case DAT_CLUSTERED:
        return (uint32_t)(dat_perm(&k->main, k->runs_used + i / k->run_len) * k->run_len + i % k->run_len);
    default:
        return (uint32_t)dat_perm(&k->main, k->n + i);
    }
}

const char *dat_pattern_name(DatPattern p);

#endif /* DAT_KEYS_H */
