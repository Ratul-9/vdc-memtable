/*
 * bench_phases.h - template for the per-structure phase loops.
 *
 * Before including, define:
 *   IMPL_T                   the structure type
 *   IMPL_PREFIX              name prefix (generates <prefix>_run_phase)
 *   IMPL_PUT(t, k, v, len)   returns MT_OK on success
 *   IMPL_GET(t, k, &v, &len) returns MT_OK / MT_NOT_FOUND / ...
 *   IMPL_DEL(t, k)           returns MT_OK on success
 * Optionally:
 *   IMPL_GET_BATCH(t, keys, n, values, lens, status)  returns number found;
 *   needs arrays bp_values / bp_lens / bp_status of BENCH_BATCH_CHUNK entries.
 */
#include "bench.h"
#include "vdc/memtable.h" /* MT_* status codes */

#define BP_CAT2(a, b) a##b
#define BP_CAT(a, b) BP_CAT2(a, b)
#define BP_FN(name) BP_CAT(IMPL_PREFIX, name)

static void BP_FN(_run_phase)(void *tp, BenchOp op, const BenchKeys *k, size_t passes, PhaseResult *out)
{
    IMPL_T *t = (IMPL_T *)tp;
    uint64_t hits = 0, sum = 0, ops = 0;
    int ok = 1;

    switch (op) {
    case OP_INSERT:
        for (size_t i = 0; i < k->n; i++)
            ok &= (IMPL_PUT(t, k->insert[i], bench_value_a, BENCH_VALUE_BYTES) == MT_OK);
        ops = k->n;
        break;

    case OP_GET_HIT:
        for (size_t p = 0; p < passes; p++) {
            for (size_t i = 0; i < k->n; i++) {
                const void *v;
                uint32_t l;
                if (IMPL_GET(t, k->access[i], &v, &l) == MT_OK) {
                    hits++;
                    sum += *(const uint8_t *)v + l;
                }
            }
        }
        ops = (uint64_t)k->n * passes;
        ok = (hits == ops);
        break;

    case OP_GET_MISS:
        for (size_t p = 0; p < passes; p++) {
            for (size_t i = 0; i < k->n_miss; i++) {
                const void *v;
                uint32_t l;
                const int rc = IMPL_GET(t, k->miss[i], &v, &l);
                hits += (rc == MT_OK);
                ok &= (rc == MT_NOT_FOUND);
            }
        }
        ops = (uint64_t)k->n_miss * passes;
        break;

    case OP_GET_BATCH:
#ifdef IMPL_GET_BATCH
        for (size_t p = 0; p < passes; p++) {
            for (size_t i = 0; i < k->n; i += BENCH_BATCH_CHUNK) {
                const size_t c = (k->n - i < BENCH_BATCH_CHUNK) ? k->n - i : BENCH_BATCH_CHUNK;
                hits += IMPL_GET_BATCH(t, k->access + i, c, bp_values, bp_lens, bp_status);
                sum += bp_lens[c - 1];
            }
        }
        ops = (uint64_t)k->n * passes;
        ok = (hits == ops);
#else
        (void)passes;
        ops = 0;
#endif
        break;

    case OP_UPDATE:
        for (size_t p = 0; p < passes; p++)
            for (size_t i = 0; i < k->n; i++)
                ok &= (IMPL_PUT(t, k->access[i], bench_value_b, BENCH_VALUE_BYTES) == MT_OK);
        ops = (uint64_t)k->n * passes;
        break;

    case OP_DELETE:
        for (size_t i = 0; i < k->n; i++)
            ok &= (IMPL_DEL(t, k->access[i]) == MT_OK);
        ops = k->n;
        break;

    default:
        break;
    }

    out->ops = ops;
    out->hits = hits;
    out->checksum = sum;
    out->ok = ok;
}

#undef BP_FN
#undef BP_CAT
#undef BP_CAT2
