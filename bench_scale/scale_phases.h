/*
 * scale_phases.h - template of the measured loops, one copy per structure.
 * Define before including:
 *   S_T                       structure type
 *   S_PREFIX                  name prefix (generates <prefix>_phase)
 *   S_PUT(t, key, v, len, s)  returns MT_OK on success
 *   S_GET(t, key, out, lenp)  copies the 32 B value to out; MT_OK / MT_NOT_FOUND
 *
 * Keys are computed in batches of SCALE_CHUNK into a 16 KB buffer (stays in L1)
 * OUTSIDE the measured region. The timer and the hardware counters run only
 * while the structure is being used, and are summed over the batches. So key
 * computation is in neither the times nor the instruction counts.
 *
 * Every value is 32 B whose first word is the key, so every get checks that it
 * got the right key.
 */
#include <string.h>

#include "dat/dat_keys.h"
#include "scale.h"
#include "vdc/memtable.h"

#ifndef SCALE_CHUNK
#define SCALE_CHUNK 4096
#endif

#define SP_CAT2(a, b) a##b
#define SP_CAT(a, b) SP_CAT2(a, b)
#define SP_FN(x) SP_CAT(S_PREFIX, x)

static uint32_t SP_FN(_keybuf)[SCALE_CHUNK];

static void SP_FN(_phase)(void *tp, int op, const PhaseIn *in, PhaseOut *out)
{
    S_T *t = (S_T *)tp;
    const DatKeySet *ks = in->keys;
    const int rnd = (in->order == ORD_RAND);
    uint32_t *keys = SP_FN(_keybuf);
    uint32_t val[8] = {0, 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u, 0x55555555u, 0x66666666u, 0x77777777u};
    uint32_t buf[8];
    uint64_t sum = 0, ns = 0;
    int ok = 1;

    const uint64_t total = (op == PH_INSERT) ? ks->n : (op == PH_GET_MISS) ? in->m_miss : in->m;
    uint64_t chunks = 0;
    int valid_chunks[PC_COUNT] = {0};
    memset(&out->rd, 0, sizeof(out->rd));

    for (uint64_t base = 0; base < total; base += SCALE_CHUNK) {
        const uint64_t len = (total - base < SCALE_CHUNK) ? total - base : SCALE_CHUNK;

        /* Not measured: compute this batch of keys. */
        if (op == PH_GET_MISS) {
            for (uint64_t j = 0; j < len; j++)
                keys[j] = dat_miss_key(ks, rnd ? dat_perm(in->ord_miss, base + j) : base + j);
        } else {
            for (uint64_t j = 0; j < len; j++)
                keys[j] = dat_key(ks, rnd ? dat_perm(in->ord, base + j) : base + j);
        }

        /* Measured: only the structure operations. */
        if (in->perf_set >= 0)
            perf_start(in->perf, in->perf_set);
        const uint64_t t0 = bench_now_ns();
        switch (op) {
        case PH_INSERT:
            for (uint64_t j = 0; j < len; j++) {
                val[0] = keys[j];
                ok &= (S_PUT(t, keys[j], val, 32, base + j + 1) == MT_OK);
            }
            break;
        case PH_GET_HIT:
            for (uint64_t j = 0; j < len; j++) {
                uint32_t vlen = 0;
                buf[0] = ~keys[j];
                const int rc = S_GET(t, keys[j], buf, &vlen);
                ok &= (rc == MT_OK) & (buf[0] == keys[j]);
                sum += vlen;
            }
            break;
        case PH_GET_MISS:
            for (uint64_t j = 0; j < len; j++)
                ok &= (S_GET(t, keys[j], buf, NULL) == MT_NOT_FOUND);
            break;
        case PH_UPDATE:
            for (uint64_t j = 0; j < len; j++) {
                val[0] = keys[j];
                val[1] = (uint32_t)(base + j);
                ok &= (S_PUT(t, keys[j], val, 32, ks->n + base + j + 1) == MT_OK);
            }
            break;
        default:
            break;
        }
        const uint64_t t1 = bench_now_ns();
        if (in->perf_set >= 0) {
            PerfReading rd;
            perf_stop(in->perf, in->perf_set, &rd);
            for (int ev = 0; ev < PC_COUNT; ev++)
                if (rd.valid[ev]) {
                    out->rd.value[ev] += rd.value[ev];
                    valid_chunks[ev]++;
                }
        }
        ns += t1 - t0;
        chunks++;
    }

    /* A counter is valid only if every batch measured it. */
    for (int ev = 0; ev < PC_COUNT; ev++)
        out->rd.valid[ev] = (chunks > 0 && (uint64_t)valid_chunks[ev] == chunks);
    out->ops = total;
    out->ns = ns;
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
