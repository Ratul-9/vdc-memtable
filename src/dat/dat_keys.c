#include "dat/dat_keys.h"

#include <string.h>

static uint64_t splitmix(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void dat_perm_init(DatPerm *p, uint64_t n, uint64_t seed)
{
    memset(p, 0, sizeof(*p));
    p->n = n > 0 ? n : 1;
    unsigned b = 0;
    while (b < 63 && ((uint64_t)1 << b) < p->n)
        b++;
    p->bits = b;
    p->mask = ((uint64_t)1 << b) - 1;
    p->shift = b / 2 + 1;
    uint64_t s = seed ^ 0x5DEECE66Dull;
    p->seed = splitmix(&s) & p->mask;
    p->k1 = splitmix(&s) | 1u;
    p->k2 = splitmix(&s) | 1u;
}

int dat_keys_init(DatKeySet *k, DatPattern pattern, uint64_t n, uint64_t domain, uint64_t run_len, uint64_t seed)
{
    memset(k, 0, sizeof(*k));
    if (n == 0 || n > domain || pattern < 0 || pattern >= DAT_PATTERN_COUNT)
        return -1;
    k->pattern = pattern;
    k->n = n;
    k->domain = domain;

    switch (pattern) {
    case DAT_DENSE:
        k->n_miss = (domain - n < n) ? domain - n : n;
        dat_perm_init(&k->miss, domain - n > 0 ? domain - n : 1, seed + 1);
        break;
    case DAT_SPARSE:
        k->n_miss = (domain - n < n) ? domain - n : n;
        dat_perm_init(&k->main, domain, seed + 2);
        break;
    case DAT_CLUSTERED: {
        if (run_len == 0 || run_len > domain)
            return -1;
        k->run_len = run_len;
        k->runs_total = domain / run_len;
        k->runs_used = (n + run_len - 1) / run_len;
        if (k->runs_used > k->runs_total)
            return -1;
        const uint64_t free_keys = (k->runs_total - k->runs_used) * run_len;
        k->n_miss = free_keys < n ? free_keys : n;
        dat_perm_init(&k->main, k->runs_total, seed + 3);
        break;
    }
    default:
        return -1;
    }
    return 0;
}

const char *dat_pattern_name(DatPattern p)
{
    static const char *const names[DAT_PATTERN_COUNT] = {"dense", "clustered", "sparse"};
    return (p >= 0 && p < DAT_PATTERN_COUNT) ? names[p] : "?";
}
