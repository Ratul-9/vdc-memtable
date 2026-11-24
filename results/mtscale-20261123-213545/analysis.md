# Analysis of experiment 6, full run (mtscale-20261123-213545)

Machine: i5-12400F, 6 cores / 12 threads, 18 MiB L3, hugetlb pages.
- keys 1M, 10M, 25M, 50M, 75M, 100M; threads 1, 2, 4, 6, 8, 12 (8 and 12 use hyper-threads);
- layouts flat and rows-2M; designs mutex, spin, nodelock, seqlock;
- workloads load, A (50/50), C (100% get), W (100% update), with uniform and zipf keys;
- 500K operations per thread, 3 repetitions.

Throughput is in M ops/s for all threads. It includes the per-operation checking work of the harness, so compare
within this run.

## 1. Checks

| Check | Result |
|---|---|
| Unit tests | 11/11 |
| Torn or misrouted reads, wrong statuses | 0, all designs, all 2,016 measurements |
| Every key present after every parallel load (up to 100M keys, 12 threads racing to create rows) | ✅ |
| Every planned size ran | ✅ 12 of 12 |
| Peak memory | 6.53 GiB (planned 6.76; budget 9) |

## 2. Winners at 100M keys, 12 threads

| workload | flat | rows-2M |
|---|---|---|
| load | spin 82.0 | spin 81.5 |
| A uniform | spin 72.6 | spin 68.9 |
| A zipf | **seqlock 130.0** | seqlock 123.7 |
| C uniform | seqlock 63.6 | spin 60.3 (seqlock 59.8) |
| C zipf | **seqlock 139.8** | seqlock 130.0 |
| W uniform | spin 81.8 | spin 82.0 |
| W zipf | **nodelock 169.2** | nodelock 168.4 (seqlock 164.1) |

## 3. The main question: does throughput hold from 1M to 100M keys?

Flat layout, cell = 6 threads / 12 threads:

| workload | design | 1M | 10M | 25M | 50M | 75M | 100M |
|---|---|---|---|---|---|---|---|
| C uniform | seqlock | 80 / 93 | 58 / 67 | 57 / 65 | 57 / 64 | 57 / 64 | 57 / 64 |
| C uniform | spin | 75 / 84 | 55 / 64 | 55 / 63 | 55 / 63 | 55 / 63 | 55 / 63 |
| W uniform | spin | 127 / 126 | 83 / 84 | 82 / 82 | 81 / 82 | 82 / 82 | 83 / 82 |
| A uniform | spin | 79 / 93 | 63 / 75 | 62 / 73 | 61 / 73 | 61 / 73 | 61 / 73 |
| load | spin | 96 / 96 | 82 / 82 | 82 / 82 | 82 / 82 | 82 / 82 | 82 / 82 |
| load | seqlock | 88 / 89 | 76 / 78 | 75 / 77 | 75 / 77 | 74 / 77 | 74 / 76 |
| C zipf | seqlock | 310 / 345 | 174 / 180 | 145 / 160 | 131 / 148 | 122 / 143 | 119 / 140 |
| W zipf | nodelock | 205 / 156 | 177 / 196 | 168 / 184 | 159 / 175 | 152 / 172 | 149 / 169 |

- **Uniform workloads: flat from 10M to 100M keys.** At most −5%, in every design and every thread count. The one
  step is from 1M to 10M, when the data stops benefiting from the caches. Beyond that, 10× more keys cost nothing.
  This is the O(1) design showing up under threads.
- **Hot keys (zipf): −22% (seqlock reads) and −14% (nodelock writes) from 10M to 100M.** With more keys, the
  popular keys are spread over more memory, so fewer of them stay in cache. That's a property of the workload, not of
  the structure.

## 4. Scaling with threads at 100M keys (flat; 1 → 6 → 12 threads)

| workload | spin | nodelock | seqlock |
|---|---|---|---|
| load | 60 → 82 → 82 (1.4×) | 28 → 74 → 77 (2.8×) | 27 → 74 → 76 (2.8×) |
| C uniform | 26 → 55 → 63 (2.4×) | 22 → 43 → 46 (2.1×) | 27 → 57 → 64 (2.4×) |
| C zipf | 36 → 68 → 52 (1.4×) | 36 → 99 → 106 (2.9×) | **43 → 119 → 140 (3.3×)** |
| W uniform | 60 → 83 → 82 (1.4×) | 28 → 75 → 79 (2.8×) | 26 → 66 → 79 (3.0×) |
| W zipf | 80 → 40 → 39 (0.5×) | **44 → 149 → 169 (3.8×)** | 42 → 139 → 164 (3.9×) |

The mutex goes from about 50 to about 10 M ops/s (0.2×) in every workload.

- **Uniform writes stop at about 82 M ops/s from 2 threads (spin) or 6 threads (node-level designs).** That is the
  machine's limit for random 64-byte writes to DRAM.
- **Uniform reads gain another 10–15% from hyper-threads** (6 → 12): more misses in flight.
- **Hot keys:** seqlock and nodelock scale 3.3–3.9×. Striped spinlocks get worse with threads (W zipf 80 → 39),
  because the hottest keys share one stripe lock.
- **Single thread:** spin beats the node-level designs by about 2× on uniform writes. An atomic instruction on a node
  that isn't in cache blocks overlapping misses. With 6+ threads the difference disappears.

## 5. Two-level layout (rows-2M) vs flat under threads (100M keys, 12 threads)

| workload | spin | nodelock | seqlock |
|---|---:|---:|---:|
| load | 0.99× | 0.97× | 0.99× |
| A uniform | 0.95× | 1.00× | 0.97× |
| C uniform | 0.95× | 0.98× | 0.94× |
| C zipf | 1.00× | 0.97× | 0.93× |
| W uniform | 1.00× | 0.99× | 0.99× |
| W zipf | 1.00× | 0.99× | 1.00× |

- **Under threads, the two-level layout costs 0–7%** at 100M keys (single-threaded: 15–20% in this harness, +30–40%
  per get in the single-threaded benchmark).
- **Parallel loads with up to 12 threads racing to create rows: 1–3% slower than flat.**

## 6. Tail latency (100M keys, 12 threads, flat; includes about 20 ns of timer overhead)

| design | uniform get p99 | uniform update p99 | zipf get p99 | zipf update p99 | zipf update p99.9 |
|---|---:|---:|---:|---:|---:|
| mutex | 13.6 µs | 9.8 µs | 11.0 µs | 10.0 µs | 15.8 µs |
| spin | 777 ns | 807 ns | 2.6 µs | 4.5 µs | **12.6 µs** |
| nodelock | 1.0 µs | 803 ns | 754 ns | 518 ns | 799 ns |
| **seqlock** | 809 ns | 799 ns | **621 ns** | 523 ns | 814 ns |

seqlock and nodelock keep p99 at or under about 1 µs and p99.9 under 1.4 µs everywhere. Spinlocks reach 12.6 µs at
p99.9 on hot writes.

## 7. The seqlock write fix (fetch_or): partly works

| W zipf, 12 threads | seqlock | nodelock | gap |
|---|---:|---:|---:|
| 1M | 88 | 156 | −44% |
| 10M | 119 | 196 | −39% |
| 25M | 153 | 184 | −17% |
| 50M | 169 | 175 | −3% |
| 100M | 164 | 169 | −3% |

- **At 50M–100M keys seqlock now matches nodelock on hot writes.**
- **At 1M–10M keys, where zipf concentrates writes on very few keys, seqlock still falls behind by 40–44%.** Its
  6 → 12 thread result at 1M even drops (167 → 88).
- **Likely cause:** waiting writers retry the `fetch_or` together the moment the version becomes even, so the hot
  line bounces between cores. A per-node ticket or queue lock for writers, or a back-off, would be the next thing to
  try. Not yet tested.

## 8. Summary

**Positive**
1. **Throughput holds from 10M to 100M keys** for uniform access, in every design, with 6 and 12 threads (within
   5%).
2. **Correct under heavy concurrency** at 100M keys: no torn reads, rows created exactly once, all keys present.
3. **The two-level layout costs only 0–7% under threads at 100M keys.**
4. **seqlock:** hot reads up to 140 M ops/s at 100M keys (345 at 1M), 3.3× scaling, p99 under 1 µs.
5. **Memory stayed in budget** (6.53 GiB peak).

**Limits**
1. **Uniform writes are capped at about 82 M ops/s** and uniform reads at about 64 M ops/s on this 6-core machine:
   the DRAM limit.
2. **Hot-key throughput falls 14–22%** as the key range grows.
3. **seqlock hot writes on small key ranges** still lose to nodelock by about 40%.
4. **Striped spinlocks and the mutex collapse under contention,** as expected.
