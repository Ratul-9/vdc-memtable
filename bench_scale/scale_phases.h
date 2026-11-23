/*
 * scale_phases.h - template of the measured loops, one copy per structure.
 * Define before including:
 *   S_T                       structure type
 *   S_PREFIX                  name prefix (generates <prefix>_phase)
 *   S_PUT(t, key, v, len, s)  returns MT_OK on success
 *   S_GET(t, key, out, lenp)  copies the 32 B value to out; MT_OK / MT_NOT_FOUND
 *
 * Every value is 32 B whose first word is the key, so every get checks that it
 * got the right key. Keys come from dat_key / dat_miss_key (no key arrays).
 */
#include "dat/dat_keys.h"
#include "scale.h"
#include "vdc/memtable.h"

#define SP_CAT2(a, b) a##b
#define SP_CAT(a, b) SP_CAT2(a, b)
#define SP_FN(x) SP_CAT(S_PREFIX, x)

static void SP_FN(_phase)(void *tp, int op, const PhaseIn *in, PhaseOut *out)
{
    S_T *t = (S_T *)tp;
    const DatKeySet *ks = in->keys;
    const int rnd = (in->order == ORD_RAND);
    uint32_t val[8] = {0, 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u, 0x55555555u, 0x66666666u, 0x77777777u};
    uint32_t buf[8];
    uint64_t sum = 0, ops = 0;
    int ok = 1;

    switch (op) {
    case PH_INSERT:
        for (uint64_t i = 0; i < ks->n; i++) {
            const uint32_t key = dat_key(ks, rnd ? dat_perm(in->ord, i) : i);
            val[0] = key;
            ok &= (S_PUT(t, key, val, 32, i + 1) == MT_OK);
        }
        ops = ks->n;
        break;
    case PH_GET_HIT:
        for (uint64_t i = 0; i < in->m; i++) {
            const uint32_t key = dat_key(ks, rnd ? dat_perm(in->ord, i) : i);
            uint32_t len = 0;
            buf[0] = ~key;
            const int rc = S_GET(t, key, buf, &len);
            ok &= (rc == MT_OK) & (buf[0] == key);
            sum += len;
        }
        ops = in->m;
        break;
    case PH_GET_MISS:
        for (uint64_t i = 0; i < in->m_miss; i++) {
            const uint32_t key = dat_miss_key(ks, rnd ? dat_perm(in->ord_miss, i) : i);
            ok &= (S_GET(t, key, buf, NULL) == MT_NOT_FOUND);
        }
        ops = in->m_miss;
        break;
    case PH_UPDATE:
        for (uint64_t i = 0; i < in->m; i++) {
            const uint32_t key = dat_key(ks, rnd ? dat_perm(in->ord, i) : i);
            val[0] = key;
            val[1] = (uint32_t)i;
            ok &= (S_PUT(t, key, val, 32, ks->n + i + 1) == MT_OK);
        }
        ops = in->m;
        break;
    default:
        break;
    }
    out->ops = ops;
    out->sum = sum;
    out->ok = ok;
}

#undef SP_FN
#undef SP_CAT
#undef SP_CAT2
#undef S_T
#undef S_PREFIX
#undef S_PUT
#undef S_GET
