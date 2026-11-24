# Analysis of experiment 6, quick run (mtscale-20261123-212531)

Machine: i5-12400F, 6 cores / 12 threads, 18 MiB L3, hugetlb pages. Quick mode:
- keys 1M, 10M, 25M; threads 1, 2, 4, 6 (one per physical core, no hyper-threads);
- layouts flat and rows-2M; designs mutex, spin, nodelock, seqlock;
- workloads load, A (50/50), C (100% get), W (100% update), with uniform and zipf keys;
- 500K operations per thread, 3 repetitions.

Throughput is in M ops/s for all threads together. This harness counts the whole per-operation loop: a value check
on every get, value construction, and a timestamp on 1 in 64 operations. So single-thread numbers are lower than in
the single-threaded scale experiments (e.g. 28 vs about 13 ns per get). Compare designs, layouts and sizes within
this run.

## 1. Checks

| Check | Result |
|---|---|
| Unit tests (incl. concurrent row creation) | 11/11 |
| Torn or misrouted reads, wrong statuses | 0 in all 4 designs |
| Every key present after every parallel load | ✅ |
| Peak memory | 1.68 GiB (budget 9 GiB) |

## 2. Winners at 25M keys, 6 threads

| workload | flat | rows-2M |
|---|---|---|
| load | spin 82.2 | spin 82.1 |
| A uniform | spin 62.0 | spin 60.1 |
| A zipf | **seqlock 147.4** | seqlock 133.2 |
| C uniform | seqlock 59.3 | seqlock 53.5 |
| C zipf | **seqlock 146.5** | seqlock 135.6 |
| W uniform | spin 84.7 | spin 84.5 |
| W zipf | **nodelock 166.8** | nodelock 148.8 |

The same picture as the 1M-key multithreaded run:
- **seqlock** wins reads and mixed work on hot keys;
- **striped spinlocks** win uniform writes;
- **nodelock** wins hot writes;
- **mutex** collapses.

## 3. Does throughput hold as the table grows? (6 threads)

| workload | design | 1M | 10M | 25M | 10M → 25M |
|---|---|---:|---:|---:|---:|
| C uniform | seqlock, flat | 79.0 | 58.1 | 59.3 | +2% |
| C uniform | spin, flat | 74.5 | 55.7 | 55.2 | −1% |
| W uniform | spin, flat | 127.1 | 84.7 | 84.7 | 0% |
| W uniform | seqlock, flat | 106.6 | 78.1 | 76.0 | −3% |
| load | spin, flat | 95.1 | 82.3 | 82.2 | 0% |
| C zipf | seqlock, flat | 311.9 | 177.1 | 146.5 | −17% |
| W zipf | nodelock, flat | 203.1 | 175.9 | 166.8 | −5% |

- **Uniform keys: flat from 10M to 25M keys.** The drop is from 1M to 10M, when the data (64 MB → 640 MB) stops
  partly benefiting from the 18 MiB L3 and the memory controller's open pages. After that every operation is one
  DRAM access, and throughput is set by how many such accesses 6 cores keep in flight (about 55–85 M ops/s).
- **Zipf keys lose more with size** (seqlock reads −17% from 10M to 25M). The hot set is spread over more keys, so
  less of it stays cached.
- **The full run (50M–100M keys, up to 12 threads) is needed** to confirm that uniform throughput stays flat to 100M.

## 4. Scaling with threads (25M keys, 1 → 6 threads)

| workload | mutex | spin | nodelock | seqlock |
|---|---:|---:|---:|---:|
| load | 0.24× | 1.37× | 2.62× | 2.62× |
| A uniform | 0.30× | 2.05× | 1.96× | 2.07× |
| A zipf | 0.27× | 0.86× | 3.33× | **3.60×** |
| C uniform | 0.23× | 2.09× | 1.87× | 2.12× |
| C zipf | 0.28× | 1.58× | 2.94× | **3.08×** |
| W uniform | 0.24× | 1.43× | 2.77× | 2.79× |
| W zipf | 0.22× | 0.47× | **3.45×** | 3.25× |

- **Uniform workloads reach 2–2.8× on 6 cores.** They are memory-bound; the limit is DRAM accesses in flight, not
  locking.
- **Striped spinlocks get slower with more threads on hot keys** (W zipf 80 → 38 M ops/s). The hottest keys 0 … 63
  share one stripe lock.
- **spin writes stop scaling at 2 threads on uniform keys** (W uniform: 59 → 87 → 85 → 85). nodelock and seqlock
  start lower (an atomic instruction on a node that is not in cache prevents overlapping misses, the effect seen
  before) but keep scaling to 6 threads.
- **The mutex collapses** to about 0.25× and has p99 latency of 4.5–6.8 µs.

## 5. rows-2M vs flat under threads (6 threads)

| workload, design | 1M | 10M | 25M |
|---|---:|---:|---:|
| C uniform, seqlock | 0.97× | 0.94× | 0.90× |
| C zipf, seqlock | 0.93× | 0.90× | 0.93× |
| W uniform, seqlock | 0.99× | 0.97× | 0.98× |
| W uniform, spin | 1.00× | 0.97× | 1.00× |
| load, seqlock | 0.94× | 0.93× | 0.94× |
| A zipf, seqlock | 0.93× | 0.92× | 0.90× |

- **The two-level layout costs 0–10% under threads.** Most is on reads; writes are almost equal.
- **Row creation in parallel loads costs little** (−6% for seqlock), even with threads racing to create rows.
- **The relative cost is smaller than single-threaded** (+30–40% per get there), because with several threads the
  memory system, not the row lookup, sets the pace.

## 6. Tail latency (25M keys, 6 threads, flat; includes about 20 ns of timer overhead)

| design | uniform get p99 | uniform update p99 | zipf get p99 | zipf update p99 | zipf update p99.9 |
|---|---:|---:|---:|---:|---:|
| mutex | 6.5 µs | 4.6 µs | 5.1 µs | 4.6 µs | 7.7 µs |
| spin | 679 ns | 561 ns | 936 ns | 2.0 µs | 4.1 µs |
| nodelock | 848 ns | 702 ns | 425 ns | 308 ns | 603 ns |
| **seqlock** | 645 ns | 678 ns | **354 ns** | 309 ns | 607 ns |

seqlock and nodelock keep p99 under 1 µs everywhere. Striped spinlocks have 2–4 µs tails on hot keys.

## 7. The seqlock write fix (fetch_or)

At 1M keys with 12 threads, the old seqlock (CAS loop) lost to nodelock on hot writes: 104.5 vs 169.2 M ops/s. This
quick run stops at 6 threads, where seqlock is 9% behind nodelock on W zipf at 25M (151.4 vs 166.8). Whether the fix
closes the gap at 12 threads will show in the full run.

## 8. Summary

- **Positive:**
  - everything is correct under threads, including racing row creation;
  - throughput holds from 10M to 25M keys for uniform access;
  - the two-level layout costs only 0–10% under threads;
  - seqlock and nodelock scale 3–3.6× on hot keys with sub-microsecond p99.
- **Limits:**
  - uniform workloads are memory-bound (about 2× on 6 cores);
  - striped locks fail on hot keys;
  - zipf throughput falls as the key range grows.
- **Next:** the full run (`scripts/scale/run_mt_scale.sh`) for 50M–100M keys and 8–12 threads.
