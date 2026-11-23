# Analysis of the multithreaded run mt-20261123-183654

Machine: Intel Core i5-12400F, **6 physical cores / 12 hyper-threads**, 18 MiB L3, Linux 6.8, GCC 11.4, hugetlb pages.
1M keys, 500K ops per worker, 3 repetitions, 2,700 measured cells (11 designs × 3 j × 6 thread counts × 3
distributions × 5 workloads). Threads 8 and 12 run on hyper-thread siblings. Main tables use j = 11.

## 1. Correctness: everything passed

| Check | Result |
|---|---|
| Unit tests on Linux | 9/9, build without warnings |
| Torn or misrouted reads, all 11 designs, all cells | 0 |
| Wrong statuses | 0 |
| Every key present after every load | yes |

## 2. Who won (j = 11, 12 threads, M ops/s)

| workload | uniform | zipf (hot keys) | interleaved (neighbouring keys) |
|---|---|---|---|
| load | rwlock 98.8 | rwlock 100.1 | nodelock 126.7 |
| A 50/50 | spin 90.4 | **seqlock 228.3** | seqlock 154.4 |
| B 95/5 | seqlock 80.3 | **seqlock 287.5** | seqlock 232.6 |
| C reads | seqlock 81.6 | **seqlock 292.0** | seqlock 246.8 |
| W writes | rwlock 127.3 | nodelock 169.2 | seqlock 121.6 |

- **The seqlock design is the overall winner**: best or tied on 11 of 15 workloads. It peaks at 292 M ops/s on hot
  reads (338 M ops/s with j = 20).
- **No delegation design wins anywhere.** The best one, sharded-async, reaches 35 M ops/s, 3–8× below the direct
  designs.

## 3. Direct designs

### Uniform keys (the data is in DRAM): everyone hits the memory wall

| uniform, 12 threads | mutex | spin | rwlock | nodelock | seqlock |
|---|---:|---:|---:|---:|---:|
| C reads | 5.1 | 79.1 | 78.3 | 59.7 | 81.6 |
| W writes | 8.8 | 125.6 | 127.3 | 117.9 | 117.5 |

Throughput stops growing at about 6 threads, which is the number of physical cores. Each operation is one random
DRAM access, so the limit is memory, not locking. Reads scale 2.8–3.2× from 1 thread.

### Hot keys (zipf): striped locks collapse, seqlock scales

| zipf, C reads | 1 thread | 6 threads | 12 threads | p99.9 at 12 threads |
|---|---:|---:|---:|---:|
| spin | 53.2 | 45.4 | 42.1 | |
| rwlock | 44.6 | 68.2 | 56.9 | |
| nodelock | 51.0 | 159.4 | 121.2 | |
| **seqlock** | 56.6 | **266.7** | **292.0** | 0.76 µs (A) |

- **Striped locks lose throughput as threads are added** (zipf W: spin 83.5 → 30.2, rwlock 86.5 → 20.3). The
  hottest keys 0 … 63 all live in stripe 0, so every thread queues on one lock word. Their p99.9 latency reaches
  13–16 µs.
- **nodelock readers write the lock byte**, so the hot Nodes bounce between cores.
- **seqlock readers only read**, so hot lines stay shared in every core's cache. Reads run 2.4× faster than
  nodelock and 7× faster than spin.

### Two weaknesses found in the seqlock / nodelock designs

1. **An atomic instruction on a Node that is not in cache stops the CPU from overlapping misses.**
   - With 1 thread and uniform keys, inserts run at 24.5 M ops/s with seqlock / nodelock, against 66 M ops/s with
     striped spinlocks. Median write latency is 98–99 ns against 33 ns.
   - On x86 a locked instruction (CAS / XCHG) waits for its cache line. When the lock is in the Node, every write
     pays the full DRAM miss one at a time. The striped lock word sits in an array that stays in cache, so the
     Node write behind it is a plain store and misses overlap.
   - The gap closes at 12 threads (93 vs 97 M ops/s), where there are enough threads to overlap misses anyway.
   - **Fix**: prefetch the Node for writing before claiming it (as `memtable_put_batch` already does).
2. **seqlock writers retry their CAS under heavy write contention.**
   - Zipf W at 12 threads: seqlock 104.5 M ops/s with p99.9 5.5 µs, against nodelock 169.2 M ops/s with p99.9
     2.1 µs.
   - **Fix**: claim the version with one `fetch_or(version, 1)` instead of a load + CAS loop. A fetch_or always
     succeeds as a single atomic operation, so the retry storm goes away.

### Global mutex

It collapses as expected: 53.6 → 8.9 M ops/s from 1 to 12 threads (0.2×), with p99 10–13 µs at 12 threads.

## 4. Delegation designs (queues)

| uniform, A 50/50 | 2 threads | 6 threads | 12 threads | p50 at 12 threads |
|---|---:|---:|---:|---:|
| mpsc-sync | 4.6 | 10.5 | 10.6 | 0.58 µs |
| mpsc-async | 20.6 | 9.9 | 12.1 | 21.7 µs |
| sharded-sync | 4.9 | 11.0 | 20.6 | 0.31 µs |
| **sharded-async** | 20.6 | 21.6 | **34.0** | 5.4 µs |
| mpmc-sync | 4.5 | 5.3 | 5.5 | 1.0 µs |
| mpmc-async | 18.8 | 9.3 | 6.7 | 28.7 µs |

Why delegation loses:

- **Every request crosses cores twice.** The request cell travels to the consumer and the completion travels back.
  - One producer–consumer pair tops out at about 22 M ops/s (45 ns per request).
  - One consumer handles at most about 12 M ops/s (about 80 ns per request): it takes its own DRAM miss per Node
    and does not overlap them.
  - A direct thread does the whole operation in 20–40 ns.
- **More producers make a single queue slower.** mpsc-async drops from 22 to 11 M ops/s when going from 1 to 3
  producers, because they fight over the queue's `enqueue_pos` line and the cells.
- **Key-range ownership breaks under skew.** With zipf, the hot keys 0, 1, 2, … all belong to consumer 0, so
  sharded designs stay flat at about 11 M ops/s at any thread count.
- **The shared MPMC queue is the worst design** (5.5–6.9 M ops/s, flat). Every request goes through one contended
  queue, and (C−1)/C of them are then forwarded, so they pass through two queues. This confirms the per-consumer
  queue choice.
- **Async latency is queueing delay**, not service time. 11 producers × 32 in flight ÷ 12 M ops/s ≈ 29 µs, which
  matches the measured p50.
- **Sync sharded has the best tail latency of all delegation designs** (p99 0.5–0.7 µs on uniform keys), but low
  throughput.

To make delegation competitive (if it is kept):
- one SPSC ring per producer–consumer pair (no CAS on `enqueue_pos`);
- consumers prefetch the Nodes of the next queued requests;
- batched completions;
- ownership by a hash of the key instead of key ranges, so hot keys spread over consumers.

## 5. VDC scatter vs identity: the hypothesis did not hold

12 threads, j = 6 / 11 / 20 (M ops/s), best VDC j relative to identity:

| workload | spin | nodelock | seqlock |
|---|---|---|---|
| C uniform | 77.5 / 79.1 / 83.2 (0.95×) | 58.9 / 59.7 / 63.8 (0.94×) | 80.2 / 81.6 / 86.4 (0.94×) |
| C interleaved | 100.7 / 229.1 / 234.9 (0.98×) | 93.7 / 120.8 / 121.6 (0.99×) | 173.1 / 246.8 / 247.2 (1.00×) |
| C zipf | 39.4 / 42.1 / 42.7 (0.99×) | 120.2 / 121.2 / 96.1 (**1.26×**) | 154.7 / 292.0 / 337.6 (0.87×) |
| W zipf | 29.5 / 30.2 / 30.3 (1.00×) | 174.0 / 169.2 / 153.7 (**1.13×**) | 153.9 / 104.5 / 112.7 (**1.36×**) |
| W interleaved | 106.1 / 114.2 / 113.1 (1.01×) | 93.0 / 120.1 / 118.6 (1.01×) | 93.7 / 121.6 / 120.6 (1.01×) |

- **j = 11 and identity are within 5% almost everywhere.** Identity is 4–7% faster on uniform keys: the reversal
  costs instructions, as in the single-threaded run, and the mutex design pays them inside its critical section.
- **j = 6 is much worse for neighbouring and hot reads** (spin interleaved reads 101 vs 229; seqlock zipf reads 155
  vs 292). The cache-set conflicts found single-threaded get worse with more threads.
- **Interleaved writers did not hurt identity.** A Node is 64 B, exactly one cache line, so neighbouring keys never
  share a line. The false sharing that scatter was meant to prevent is already prevented by the Node size.
- **The only wins are hot-key workloads with per-node locking**: nodelock 1.13–1.26×, and seqlock writes with j = 6
  at 1.36×. These are specific and modest, and the cause is not yet shown. A likely candidate is neighbouring hot
  Nodes sharing a 128 B pair fetched by the L2 spatial prefetcher. `perf c2c` on zipf W, j = 6 vs j = 20, would
  confirm or rule it out.

**Conclusion**: as a concurrency argument, scatter is not the selling point. What does carry the design under
threads is the combination of O(1) addressing, one cache line per operation, and a version word inside the Node that
gives lock-free reads (seqlock).

## 6. Context against published numbers (per physical core, not directly comparable)

| | M ops/s | per core | setup |
|---|---:|---:|---|
| VDC seqlock, zipf reads | 292 | 49 | 6 cores, 1M keys, single socket |
| VDC best, uniform reads (DRAM) | 82 | 14 | same |
| B-skiplist reads (YCSB-C) | 74.1 | 1.2 | 64 cores, 100M keys, 2 sockets |

Different data size, keys and machine: use only as a sanity check that the numbers are in a plausible range.

## 7. Benchmark issues to fix

- `forwarded` in the CSV is cumulative over the service's lifetime, not per cell (it reads 10M–75M). Report the
  per-cell difference.
- Threads 8 and 12 share physical cores. A machine with ≥ 16 physical cores is needed to see real scaling past 6.
- No hardware counters in this driver. Add `perf c2c` runs for the scatter question and the seqlock write retries.

## 8. Recommended next steps

1. Make **seqlock** the default concurrent design, with two fixes: `fetch_or` to claim the version, and a write
   prefetch of the Node before claiming it. Re-run zipf W and uniform load.
2. Drop the shared-MPMC design. If delegation stays, rebuild sharded with SPSC rings, consumer-side prefetch and
   hash ownership.
3. Run `perf c2c` on zipf W (j = 6 vs 20) to explain the only scatter wins.
4. Fix the `forwarded` counter, then re-run on a machine with more physical cores.
