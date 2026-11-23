# DAT experiments: everything done so far

A complete record of the VDC / direct-address-table memtable project: the idea, how it changed, what was built,
every experiment, every baseline, every result, and what it means for a paper. Written in plain terms, with the
exact numbers.

Status: 23 Nov 2026. Single-threaded and multithreaded experiments are done on one physical machine. The next
planned step (section 12) is a two-level "matrix" layout and scale tests up to 100M keys.

---

## Contents

1. The idea in one paragraph
2. How the design changed
3. The final design
4. Machines used
5. What was built (code)
6. Baselines
7. Experiment method
8. Single-threaded results
9. Multithreaded results
10. Comparison with published papers
11. Conclusions: what worked and what did not
12. Next: two-level layout and 10M–100M keys
13. Paper readiness
14. File map

---

## 1. The idea in one paragraph

A memtable (the in-memory write buffer of an LSM key-value store) where a key's position is **computed, not
searched**. Keys are integers in a fixed range (0 … 2^20 − 1). The slot of a key is its Van der Corput number, which
in binary is bit reversal: `slot = reverse(key)`. Every key has exactly one slot, so there are no collisions, no
hashing and no probing. Lookup, insert, update and delete each need one address computation and one memory access:
O(1) in the worst case, not just on average. The bit reversal was meant to scatter neighbouring keys across memory.

## 2. How the design changed

| Step | Idea | Why it changed |
|---|---|---|
| 1 | 1024 slots, hashed keys + linear probing, VDC as a "slot estimator" | Hashing arbitrary keys into a fixed table always collides (50% chance of a collision with only ~1,200 keys in 2^20 slots), and probing breaks worst-case O(1) |
| 2 | **Direct addressing**: keys are integers 0 … 2^20 − 1, `slot = VDC(key)` | Any permutation of 20-bit numbers is one-to-one, so there are no collisions, ever |
| 3 | **Partial reversal**: keep the low j key bits, reverse only the upper 20 − j bits | Full reversal throws away all locality. Partial reversal keeps runs of 2^j consecutive keys together. j = 0 is full reversal, j = 20 is identity (`slot = key`) |
| 4 | 64 B Node = one cache line | No Node ever straddles two lines; one operation touches one line |
| 5 | Huge pages, populate, prefetch, bitmap | From a 30-item list of memory / cache problems (file `memory and cache related problems.md`) |
| 6 | Concurrent versions | 5 locking designs + 6 queue-based (delegation) designs |
| 7 | (planned) two-level matrix with on-demand rows | Sorted order, memory that grows with the data, larger key ranges |

## 3. The final design

### Key → slot mapping (`include/vdc/vdc.h`)

```
slot = (reverse_{20-j}(key >> j) << j) | (key & (2^j − 1))
```

- j = 0: full bit reversal. j = 20: identity (`slot = key`). Tested: j = 6, 8, 11, 15, 20.
- Always one-to-one on 0 … 2^20 − 1 (checked exhaustively for every j), and its own inverse.

### Node (one slot) (`include/vdc/slot.h`): exactly 64 B, 64 B aligned

| Offset | Field | Size | Purpose |
|---:|---|---:|---|
| 0 | key | 3 B | 20-bit key stored in 24 bits |
| 3 | lock | 1 B | per-node lock byte (concurrent node-lock design) |
| 4 | version | 4 B | seqlock version (even = stable) |
| 8 | seqno | 8 B | write sequence number |
| 16 | ext_off | 8 B | arena offset for values > 32 B |
| 24 | val_len | 4 B | value length |
| 28 | flags | 4 B | OCCUPIED, TOMBSTONE, EXTERNAL |
| 32 | value | 32 B | inline value |

### VDC-Space and memtable

- **VDC-Space:** 2^20 Nodes = 64 MiB, one flat array. It is 32 huge pages of 2 MiB.
- **Memtable** (`memtable.h`):
  - put / get / delete;
  - delete writes a tombstone, so it can hide older data in lower LSM levels;
  - get tells "never written" apart from "deleted";
  - one version per key;
  - values up to 32 B inline, larger ones in a bump arena;
  - statistics;
  - `memtable_clear` to reuse the memory;
  - batched get / put with software prefetch (distance 16).
- **Platform layer** (`platform.h`):
  - base pages, with transparent huge pages switched off per mapping;
  - or the reserved 2 MiB hugetlb pool, failing loudly if it is missing;
  - optional pre-commit of all memory (populate);
  - thread pinning.
- **Occupancy bitmap** (`bitmap.h`): 1 bit per slot plus a summary level. Built and tested, but not used by the
  memtable yet.

### Decisions made along the way

| Decision | Choice |
|---|---|
| Language | C |
| Key domain | integers 0 … 2^20 − 1 (one memtable) |
| Node size | 64 B, one cache line |
| Values in benchmarks | 32 B inline only |
| Page modes tested | base (4 KiB) and hugetlb (2 MiB, reserved pool) |
| Memory commit | populate at creation (no page faults while measuring) |
| Bitmap | built, not wired into the memtable |
| Key counts (single-threaded) | 1K, 10K, 100K, 500K, 1M (2M skipped: more than one memtable holds) |
| j values | 6, 8, 11, 15, 20 |
| Concurrency designs | all four lock types, both queue shapes, sync and async producers, plus a lock-free (seqlock) version |

---

## 4. Machines used

| Machine | CPU | Cores | Caches | Counters | Used for |
|---|---|---|---|---|---|
| Azure VM | Intel Xeon Platinum 8573C | 4 vCPU | L2 2 MiB, **L3 260 MiB** | none (VM hides them) | first single-threaded run, timings only |
| **Physical machine** | Intel Core i5-12400F (Alder Lake) | **6 cores / 12 threads** | L1D 48 KiB 12-way, L2 1.25 MiB 10-way, **L3 18 MiB** | all 7 | main single-threaded and multithreaded results |
| Development laptop | Apple M5 | – | 128 B lines, 16 KiB pages | – | building, unit tests, smoke runs only |

On the Azure VM the whole 64 MiB memtable fit in L3, so its "large" results are L3 speed. On the physical machine
the L3 is smaller than the memtable, so from 500K keys up the data is really in DRAM.

---

## 5. What was built (code)

| Part | Files | What it does |
|---|---|---|
| Library | `include/vdc/*.h`, `src/*.c` | mapping, Node, Space, arena, memtable, bitmap, platform layer |
| Concurrent memtables | `concurrent.h/.c` | mutex, striped spinlocks, striped reader-writer locks, per-node lock, seqlock |
| Lock-free queues | `queue.h/.c` | Vyukov bounded MPMC ring + single-consumer pop (MPSC), 64 B cells |
| Delegation service | `service.h/.c` | key owners fed by queues: MPSC, sharded MPSC, shared MPMC; sync and async |
| Baselines | `baselines/` | linear-probing hash table, skip list (identity = vdc with j = 20) |
| Unit tests | `tests/` | 9 groups: mapping, Node, Space, arena, memtable, bitmap, platform, concurrency (also under ThreadSanitizer) |
| Single-thread benchmark | `bench/` → `build/vdc_bench` | timings + 7 hardware counters, markdown tables, CSV |
| Multithreaded benchmark | `bench_mt/` → `build/vdc_mtbench` | 11 designs, thread counts, workloads, latency percentiles |
| Experiments | `experiments/01–07` | bijection, cache-set aliasing model, huge pages, prefetch, layout, fill cost, baselines |
| Scripts | `scripts/` | `setup_hugepages.sh`, `perf_counters.sh`, `run_bench.sh`, `run_mt_bench.sh`, `plot_results.py` |

---

## 6. Baselines

| Baseline | What it is | Why it is there |
|---|---|---|
| **Identity (j = 20)** | the same memtable with `slot = key` | tells whether the VDC permutation adds anything |
| **Hash table** | open addressing, linear probing, 64 B entries, 2^21 entries (load ≤ 0.5 at 1M), MurmurHash3 finalizer, same tombstone semantics | the standard expected-O(1) structure |
| **Skip list** | LevelDB / RocksDB style, p = 1/4, max height 16, nodes in a bump arena | the default RocksDB memtable (O(log n)) |
| **Concurrent**: global mutex, striped locks | see section 9 | standard locking baselines |

Sanity check that the skip list baseline is fair:

| | Our skip list | Published |
|---|---:|---:|
| LLC misses per op | about 20 (13 insert, 26 get) | Folly skip list 24.5 (B-skiplist paper) |
| Insert time, 1 thread | 0.49 µs | RocksDB SkipList 0.80 µs (Aparajita, `memtablerep_bench`) |

All baselines use the same 32 B values, the same tombstone semantics and the same page modes.

---

## 7. Experiment method

### Single-threaded (`vdc_bench`)

- **Variants:** j = 6, 8, 11, 15, 20; plus the hash table and skip list in the baseline run.
- **Key counts:** 1K, 10K, 100K, 500K, 1M (K = 1,000, M = 1,000,000).
- **Patterns:**
  - seq: keys 0 … N−1 in order;
  - dense_rand: keys 0 … N−1 in random order;
  - rand: N random keys from the whole range.
- **Operations:**
  - insert into an empty table;
  - get_hit (key present);
  - get_miss (never written);
  - get_batch (prefetching batch API);
  - update;
  - delete (tombstone).
- **Page modes:** base and hugetlb.
- **Measurement:**
  - read and update phases repeat until at least 1M operations ran; insert and delete run once;
  - 5 repetitions, medians;
  - hardware counters in 3 groups of at most 3 events, so short runs are exact, not multiplexed;
  - memory committed before measuring.
- **Counters:** cycles, instructions, L1D load misses, LLC misses, dTLB load misses, dTLB store misses, page faults.
- **O(1) test:** instructions per operation must stay flat from 1K to 1M keys (max / min ≤ 1.10). Time per operation
  is allowed to grow, because the data moves from L1 to DRAM; that is the memory hierarchy, not the algorithm.
- **Built-in checks:**
  - every operation returns the expected status;
  - LLC misses ≤ 1.25 per op;
  - dTLB misses ≤ 0.05 per op on hugetlb;
  - 0 page faults.

### Multithreaded (`vdc_mtbench`)

- **Designs:** 11 in total (section 9).
- **Threads:** 1, 2, 4, 6, 8, 12 in total, consumers included. Delegation designs use half of the threads as
  consumers (MPSC: one).
- **j values:** 6, 11, 20.
- **Data:** 1M keys, all present before the read / update workloads.
- **Workloads:**
  - load (insert every key);
  - A: 50% get / 50% update;
  - B: 95 / 5;
  - C: 100% get;
  - W: 100% update.
- **Key distributions:**
  - uniform;
  - zipf θ = 0.99, hottest keys 0, 1, 2, …;
  - interleaved: worker w uses keys w, w+W, w+2W, …, so all threads touch neighbouring keys.
- **Per cell:** 500K operations per worker or producer, 3 repetitions, 1 in 64 operations timed for latency.
- **Checks:** every value stored is four equal 64-bit words containing the key, so a torn or misrouted read is
  detected on every get.

---

## 8. Single-threaded results

### 8.1 Azure VM (timings only, data stayed in L3)

| Finding | Number |
|---|---|
| Correctness | all operations correct, 0 page faults |
| Random gets, hugetlb, j = 11 | about 5 ns up to 10K keys (L2), about 10 ns from 100K to 1M (L3): **+3% from 100K to 1M** |
| VDC vs hash vs skip list, gets at 1M | 9.9 vs 18.6 vs 735 ns |
| Sequential gets, any N | vdc flat at about 5 ns; hash grows 4.5 → 23.7 ns |
| Bit reversal cost | identity (j = 20) 35–45% faster than any other j, **even at 1K keys in L1** |
| Cache-set aliasing | dense keys, 10K keys, hugetlb: j = 6 / 8 9.6 ns vs j = 11 / 15 5.1 ns (predicted by the analytic model) |
| Huge pages | 15–30% faster from 100K keys |
| Prefetch | best distance 8–64; 1.3–1.9× faster |

### 8.2 Physical machine (all counters)

**Checks**

| Check | Result |
|---|---|
| Unit tests | 8/8 |
| Correctness | ✅ every cell (vdc, hash, skip list) |
| **O(1): instructions per op flat 1K → 1M (vdc)** | ✅ **worst ratio 1.01** (get: 81.0 instructions at 1K and at 1M) |
| O(1) hash table | ❌ 1.31 (misses: 52 → 68 instructions as probe chains grow) |
| O(1) skip list | ❌ 2.42 (get: 189 → 340 instructions, O(log n)) |
| **TLB on hugetlb** | ✅ worst 0.004 dTLB misses per op |
| Page faults while measuring | ✅ 0 |
| Cache: LLC misses per op ≤ 1.25 | ❌ worst 2.68, but L1D misses are exactly 1.000 per op (see below) |

**Time per operation**, random keys, hugetlb, j = 11 (ns/op):

| keys | get_hit | get_miss | insert | update | delete | get_batch |
|---|---:|---:|---:|---:|---:|---:|
| 1K | 3.8 | 3.1 | 24.4 | 6.4 | 3.7 | 6.0 |
| 10K | 4.0 | 3.4 | 16.4 | 6.5 | 3.8 | 6.1 |
| 100K | 5.8 | 7.4 | 17.0 | 15.4 | 13.6 | 6.2 |
| 500K | 21.6 | 17.6 | 29.7 | 28.7 | 21.4 | 8.5 |
| 1M | 21.7 | 5.7* | 29.4 | 28.7 | 22.3 | 8.8 |

\* Only 48,576 never-written keys exist at 1M, and they stay cached, so get_miss at 1M is not comparable.

**Cache: one line per operation is achieved.**
- L1D load misses are exactly 1.000 per op for every operation and every j (hugetlb).
- The generic LLC counter shows 2.0–2.5 for plain gets. It also counts lines that the hardware's L2 spatial
  prefetcher fetches on its own (the neighbouring line of each 128 B pair).
- Batched gets (software prefetch) and writes on hugetlb show 1.0.
- On base pages, writes show about 1.5; the extra 0.5 is page-table walks.

**The bit reversal costs 26 instructions per operation:**

| op | j = 6 … 15 | j = 20 (identity) |
|---|---:|---:|
| get_hit | 81 | 55 |
| get_miss | 66 | 40 |
| insert | 140 | 114 |
| get_batch | 134 | 82 (the slot is computed twice) |

- In cache this costs about 1.5 ns per op.
- In DRAM identity is 30% faster on plain gets (15.4 vs 21.7 ns), because fewer instructions let more cache misses
  overlap.
- With the batch API they are equal (8.8 vs 9.1 ns).
- Cause: x86 has no bit-reverse instruction and GCC has no builtin, so the reversal is a 5-step shift / mask.

**Choice of j: cache-set aliasing confirmed in L2 and L3.** Dense keys, 100K keys, hugetlb:

| | j=6 | j=8 | j=11 | j=15 | j=20 |
|---|---:|---:|---:|---:|---:|
| get_hit ns/op | 20.3 | 19.9 | **5.6** | 6.3 | 4.6 |
| get_hit LLC misses/op | 1.89 | 1.51 | **0.006** | 0.027 | 0.079 |

- A 6.4 MiB working set that fits in the 18 MiB L3 runs **3.5× slower with j = 6 / 8**.
- The cause: partial reversal leaves some slot bits constant for dense key ranges, so caches indexed by those bits
  lose capacity.
- Sequential keys: j = 6 has about 1.2 LLC misses per op from 100K keys up; j ≥ 11 has 0.01–0.05.
- Random keys: all j are equal.
- **Decision: j = 11** (as good as identity for dense keys).

**Huge pages:**

| rand keys | dTLB misses/op, base | dTLB misses/op, hugetlb |
|---|---:|---:|
| 10K to 1M, every op, every j | 0.86 – 0.89 | **0.000** |

| get_hit, j = 11 | base | hugetlb |
|---|---:|---:|
| 100K (in L3) | 10.5 ns | 5.8 ns (−45%) |
| 1M (in DRAM) | 22.8 ns | 21.7 ns (−5%) |

Huge pages remove every TLB miss. The time gain is large while the data is in cache and small in DRAM, where the CPU
overlaps the page walks with the memory access.

**Prefetch distance** (experiment 04, 1M keys, ns/op):

| distance | base rand | hugetlb rand |
|---|---:|---:|
| 0 (none) | 18.7 | 17.2 |
| 8 | 13.0 | 10.3 |
| 16 | 8.9 | 7.8 |
| **32** | **8.1** | **7.1** |
| 64 | 8.3 | 7.3 |

- 2.0–2.4× faster, with distance 32 best on a DRAM-bound machine.
- Below about 100K keys the batch API is slower than plain gets (fixed overhead).

**Memory commit** (experiment 06, 1M random inserts):

| pages | populate | create | page faults during inserts | insert ns/op |
|---|---|---:|---:|---:|
| base | no | 0.01 ms | 31,250 | 56.2 |
| base | yes | 18.0 ms | 0 | 30.2 |
| hugetlb | no | 0.01 ms | 64 | 36.7 |
| hugetlb | yes | 8.5 ms | 0 | 27.0 |

- Lazy pages fault twice per page: the read of a Node maps the shared zero page, and the write faults again.
- Clearing 64 MiB for reuse takes 8.5 ms.

**Baselines** (random keys, hugetlb unless noted, 1M keys):

| | vdc j=11 | identity | hash table | skip list |
|---|---:|---:|---:|---:|
| get_hit ns | 21.4 | 14.9 | 25.1 | 643 |
| get_miss ns | 5.5 | 3.1 | 31.0 | 623 |
| insert ns | 27.8 | 23.2 | 30.5 | 486 |
| get_hit instructions | 81 | 55 | 72.5 | 340 |
| get_hit LLC misses | 2.31 | 1.78 | 4.83 | 26.8 |
| seq get_hit ns (base pages) | 5.2 | 5.1 | 29.2 | 67 |

- vs hash table: 1.2× on random gets, **5.6× on missing keys, 5.6× on sequential keys**.
- vs skip list: 30× on gets, 17.5× on inserts.

### 8.3 Analytic experiments (no hardware needed)

- **01 Bijection:** every j is one-to-one. Pages touched by keys 0 … N−1: every j ≥ 6 reaches the 4 KiB minimum;
  on 2 MiB pages j = 6 touches 16 pages for 1K keys, while j = 11 touches 1.
- **02 Cache-set model:** largest number of keys in one cache set for windows of W consecutive keys (i5-12400F, L2
  10-way):

| j | W=1024 | W=4096 | W=16384 |
|---|---:|---:|---:|
| 6 | 16 ❌ | 64 ❌ | 256 ❌ |
| 8 | 4 ✅ | 16 ❌ | 64 ❌ |
| 11, 15, 20 | 1 ✅ | 2 ✅ | 8 ✅ |

This model correctly predicted the measured slowdowns.

---

## 9. Multithreaded results

### 9.1 Designs

| Design | How it works |
|---|---|
| mutex | one global lock |
| spin | one spinlock per 64 slots (16,384 locks, each on its own cache line) |
| rwlock | same stripes, readers share them |
| nodelock | lock byte inside each Node |
| **seqlock** | readers check `version` (even = stable), copy the Node, re-check, and never write; writers claim the Node with a CAS |
| mpsc-sync / -async | one consumer owns all keys, one MPSC queue |
| sharded-sync / -async | C consumers, each owns a key range, one MPSC queue each |
| mpmc-sync / -async | C consumers share one MPMC queue and forward other owners' keys |

- **sync:** the producer waits for every result.
- **async:** the producer keeps up to 32 requests in flight.

Run on the i5-12400F: 6 cores / 12 threads, hugetlb, 2,700 measured cells.

### 9.2 Correctness

0 torn or misrouted reads, 0 wrong statuses, every key present after every load, in all 11 designs. The unit tests
also pass under ThreadSanitizer.

### 9.3 Fastest design at 12 threads (j = 11, M ops/s)

| workload | uniform | zipf | interleaved |
|---|---|---|---|
| load | rwlock 98.8 | rwlock 100.1 | nodelock 126.7 |
| A 50/50 | spin 90.4 | **seqlock 228.3** | seqlock 154.4 |
| B 95/5 | seqlock 80.3 | **seqlock 287.5** | seqlock 232.6 |
| C reads | seqlock 81.6 | **seqlock 292.0** | seqlock 246.8 |
| W writes | rwlock 127.3 | nodelock 169.2 | seqlock 121.6 |

### 9.4 Main findings

- **Seqlock wins overall** (best or tied on 11 of 15 workloads). Its readers never write, so hot Nodes stay shared
  in every core's cache. Hot-key reads reach 292 M ops/s, 338 with j = 20:

| zipf, C reads (M ops/s) | 1 thread | 6 threads | 12 threads |
|---|---:|---:|---:|
| spin | 53.2 | 45.4 | 42.1 |
| rwlock | 44.6 | 68.2 | 56.9 |
| nodelock | 51.0 | 159.4 | 121.2 |
| seqlock | 56.6 | 266.7 | 292.0 |

- **Uniform keys hit the memory wall.** Every good direct design plateaus at 80–127 M ops/s from 6 threads, one per
  physical core.
- **Striped locks collapse on hot keys.** Keys 0 … 63 share stripe 0 (zipf W: spin 83.5 → 30.2, rwlock 86.5 → 20.3
  M ops/s). p99.9 latency reaches 13–16 µs.
- **The mutex collapses:** 53.6 → 8.9 M ops/s, p99 10–13 µs.
- **Seqlock weakness 1:** an atomic instruction on a Node that is not in cache stops the CPU from overlapping
  misses. 1 thread, uniform inserts: 24.5 M ops/s vs 66 for striped spinlocks; write p50 98 vs 33 ns. Fix: prefetch
  the Node before claiming it.
- **Seqlock weakness 2:** under heavy hot-key writes the CAS keeps retrying (zipf W, 12 threads: 104.5 vs nodelock
  169.2 M ops/s; p99.9 5.5 vs 2.1 µs). Fix: claim with one `fetch_or`.
- **Delegation (queues) loses everywhere, by 3–8×:**

| uniform, A 50/50 | 2 threads | 6 | 12 | p50 at 12 |
|---|---:|---:|---:|---:|
| mpsc-sync | 4.6 | 10.5 | 10.6 | 0.58 µs |
| mpsc-async | 20.6 | 9.9 | 12.1 | 21.7 µs |
| sharded-sync | 4.9 | 11.0 | 20.6 | 0.31 µs |
| sharded-async | 20.6 | 21.6 | 34.0 | 5.4 µs |
| mpmc-sync | 4.5 | 5.3 | 5.5 | 1.0 µs |
| mpmc-async | 18.8 | 9.3 | 6.7 | 28.7 µs |

  Why:
  - **Two cross-core trips per request:** the request goes to the consumer and the result comes back. One consumer
    caps at about 12 M ops/s (about 80 ns per request).
  - **More producers slow a single queue:** mpsc-async 22 → 11 M ops/s.
  - **Key-range ownership puts all zipf hot keys on consumer 0:** flat at about 11 M ops/s.
  - **The shared MPMC queue is worst** (5.5–7 M ops/s), because most requests are forwarded through a second queue.
  - **Async latency is queueing delay:** 11 producers × 32 in flight ÷ 12 M ops/s ≈ 29 µs.

### 9.5 VDC scatter vs identity under threads: hypothesis not confirmed

12 threads, j = 6 / 11 / 20 (M ops/s), best VDC j relative to identity:

| workload | spin | nodelock | seqlock |
|---|---|---|---|
| C uniform | 77.5 / 79.1 / 83.2 (0.95×) | 58.9 / 59.7 / 63.8 (0.94×) | 80.2 / 81.6 / 86.4 (0.94×) |
| C interleaved | 100.7 / 229.1 / 234.9 (0.98×) | 93.7 / 120.8 / 121.6 (0.99×) | 173.1 / 246.8 / 247.2 (1.00×) |
| C zipf | 39.4 / 42.1 / 42.7 (0.99×) | 120.2 / 121.2 / 96.1 (1.26×) | 154.7 / 292.0 / 337.6 (0.87×) |
| W zipf | 29.5 / 30.2 / 30.3 (1.00×) | 174.0 / 169.2 / 153.7 (1.13×) | 153.9 / 104.5 / 112.7 (1.36×) |
| W interleaved | 106.1 / 114.2 / 113.1 (1.01×) | 93.0 / 120.1 / 118.6 (1.01×) | 93.7 / 121.6 / 120.6 (1.01×) |

- **j = 11 and identity are within 5%** almost everywhere; identity is 4–7% faster on uniform keys.
- **j = 6 is much worse** on neighbouring and hot keys (the cache-set conflicts again).
- **Why scatter did not help:** a Node is exactly one cache line, so neighbouring keys never share a line. The false
  sharing that scatter was meant to prevent is already prevented by the Node size.
- **The only wins** are hot-key writes with per-node locking (1.13–1.36×). The cause is not yet shown; `perf c2c`
  would show it.

### 9.6 Benchmark issue

The `forwarded` column in the CSV is cumulative over a service's lifetime, not per cell. Don't use it.

---

## 10. Comparison with published papers

All paper numbers are the authors' claims, on other machines. Ratios against each paper's own baseline and
per-operation counters are the fair comparisons.

### 10.1 Improvement over each paper's own baseline

| Design | Where | Improvement | Threads |
|---|---|---|---|
| Arctic (OSDI '26) | in RocksDB | up to 1.40× writes; 1.3× reads vs ART | 80 |
| Aparajita (preprint '26) | in RocksDB | 1.22–1.39× reads; about 1.0× multithreaded writes | 1–64 |
| MountDB (arXiv '26) | in RocksDB | up to 1.77× reads, 1.3× writes | – |
| JellyFish (Middleware '20) | in RocksDB | 1.51× reads, 1.16× updates | 16 |
| FloDB (EuroSys '17) | in LevelDB | 1.9–3.5× writes | 16+ |
| Foresight (arXiv '26) | standalone | up to 1.45× | 128 |
| B-skiplist (ICPP '25) | standalone | 3.2× reads, 2.1× inserts vs Folly | 128 |
| VDC (ours) | standalone | 30× reads vs skip list; 1.2× vs hash table | 1 |
| VDC seqlock (ours) | standalone | 7× vs striped locks on hot reads; 5.2× scaling 1 → 12 threads | 12 (6 cores) |

The 30× is expected for any direct-addressed array. The fair standalone comparison is the hash table.

### 10.2 Cache misses per operation (the most comparable number)

| Design | LLC misses per op |
|---|---:|
| Folly skip list | 24.5 |
| BP-tree | 10.5 |
| B-skiplist | 7.5 |
| Our skip list | about 20 |
| Our hash table | 4.8 |
| **VDC** | **1 cache line per op** (L1D misses = 1.000) |

No published design reaches one line per operation.

### 10.3 Per physical core (sanity check only, not comparable)

| | M ops/s per core | Setup |
|---|---:|---|
| Folly reads | 0.36 | 64 cores, 100M keys |
| B-skiplist reads | 1.16 | 64 cores, 100M keys |
| VDC seqlock hot reads | 49 | 6 cores, 1M keys |
| VDC uniform reads (DRAM) | 14 | 6 cores, 1M keys |

### 10.4 Features

| | VDC | B-skiplist | Foresight | Arctic | Aparajita | JellyFish | MountDB | FloDB |
|---|---|---|---|---|---|---|---|---|
| Worst-case point op | **O(1)** | O(log n) | O(log n) | O(key length) | linear in node | O(log n) | learned | O(1) + O(log n) |
| Any keys | ❌ dense 20-bit ints | ints | ≤ 8 B | ✅ | ✅ | ✅ | numeric | ✅ |
| Range scans | ❌ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Versions / snapshots | ❌ | ❌ | ❌ | – | ✅ | ✅ | ✅ | ✅ |
| Lock-free reads | ✅ seqlock | ❌ | ✅ | ✅ | ❌ | ✅ | – | ✅ |
| Inside a storage engine | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Capacity | 1M keys, fixed 64 MiB | 100M+ | 33M+ | 100M+ | 2M+ | 1M+ | 64 MB buffers | GBs |

### 10.5 Estimate inside RocksDB (not measured)

Based on Aparajita's single-thread numbers, VDC might reach 1.8–3.9× on inserts and 1.4–2× on reads. With many
threads, writes would likely tie, because RocksDB's own write path is the bottleneck.

Blockers:
- versions per key;
- string keys (db_bench generates dense integer keys, so it would work there);
- a 2M key space;
- memory per key.

---

## 11. Conclusions: what worked and what did not

### Did VDC help?

| Goal | Delivered? | By what |
|---|---|---|
| O(1) lookup, insert, update | ✅ | direct addressing (identity gives the same) |
| No collisions | ✅ | any one-to-one mapping (identity too) |
| Few cache / TLB misses | ✅ | 64 B Node + huge pages |
| VDC faster than `slot = key` | ❌ | identity is about 30% faster (26 fewer instructions) |
| Scatter helps locality | ❌ | small j causes cache-set conflicts (3.5× slower) |
| Scatter helps under threads | ❌, mostly | within 5% of identity; small wins only on hot-key writes |

**In one line:** the design meets its goals because of direct addressing, the one-line Node, huge pages and the
in-Node version word (lock-free reads). The VDC permutation itself added cost almost everywhere.

### Solid, reusable results

1. **One cache line per operation**, measured (L1D misses = 1.000).
2. **O(1) proven with instruction counts**, flat from 1K to 1M keys. Hash table and skip list fail the same test.
3. **A cache-set aliasing model** that predicts when a bit-permuted layout loses cache capacity, confirmed in L2 and
   L3.
4. **Seqlock inside the Node**: lock-free reads, 292 M ops/s on hot reads with 6 cores.
5. **Concurrency lessons:**
   - atomic instructions on uncached lines block overlapping misses;
   - delegation loses 3–8× to direct access on one socket;
   - key-range ownership breaks under skew;
   - a shared MPMC queue is the worst choice.

---

## 12. Next: two-level layout and 10M–100M keys (built, not run yet)

**Status:** experiments 1–5 are implemented (`build/dat_scale`, `scripts/scale/run_scale.sh`) under a 9 GiB memory
budget for a 16 GB machine. See [SCALE_EXPERIMENTS.md](SCALE_EXPERIMENTS.md) for how to run them and what happens in
memory. Implementation choices beyond the plan below:
- the row table uses 4-byte entries, so the sizes are 8 MiB / 512 KiB / 16 KiB (still L3 / L2 / L1);
- keys are computed on the fly, so there are no key lists;
- the new 64 B node `DatNode` has a 4-byte key; the 1M-key code is unchanged;
- the flat array on base pages only reserves the key range, and memory grows with touched 4 KiB pages.

### 12.1 The proposed layout

A "matrix": row = high bits of the key, column = low bits.

- **Dense matrix:** the address is `row × C + col = key`, which is the identity array. It is just as fast and
  sorted, but not new.
- **Rows allocated on demand:** a small row table of pointers, and each row a block of Nodes created when its first
  key arrives. It is like a page table:
  - still O(1) (always 2 steps);
  - sorted order, so range scans and flush come for free;
  - memory that grows with the data;
  - room for larger key ranges.
- **Cost:** a second memory access per operation unless the row table stays in cache.
- The mapping should use compile-time constants (shifts / masks), not a runtime j.
- Bit rotations or permutations would break sorted order: sorted order and scatter cannot both be had.

| | Dense matrix | On-demand rows |
|---|---|---|
| O(1) | ✅ 1 access | ✅ 2 accesses |
| Sorted / scans / flush | ✅ | ✅ |
| Memory | fixed, whole range | grows with used rows |
| Key range | 2^20 | larger with more levels |
| Cache misses | 1 line | 2 lines unless the table is cached |
| Hot / sequential keys | cluster in one row | same |
| Novelty | none (identity) | close to radix trees / page tables; new in the memtable design around it |

### 12.2 What the scale-up needs

- **27-bit keys** (100M < 2^27). The 3-byte key field must grow to 4 bytes, which moves the lock byte.
- **Memory** (64 B per key):

| Keys | Nodes | Benchmark key lists | Total |
|---|---:|---:|---:|
| 10M | 0.64 GB | 0.16 GB | 0.8 GB |
| 25M | 1.6 GB | 0.4 GB | 2.0 GB |
| 50M | 3.2 GB | 0.8 GB | 4.0 GB |
| 75M | 4.8 GB | 1.2 GB | 6.0 GB |
| 100M | 6.4 GB | 1.6 GB | 8.0 GB |

- **The row size decides where the row table lives** (2^27 key range, 8 B per row pointer):

| Row size | Keys per row | Rows | Table | Fits in |
|---|---:|---:|---:|---|
| 4 KiB | 64 | 2,097,152 | 16 MiB | L3 (competing with the data) |
| 64 KiB | 1,024 | 131,072 | 1 MiB | L2 |
| 2 MiB | 32,768 | 4,096 | 32 KiB | L1 |

### 12.3 Experiment plan

| # | Question | Runs | Look at |
|---|---|---|---|
| 1 | How does the flat array scale? (reference) | 1M, 10M, 25M, 50M, 75M, 100M; dense keys; sequential and random; insert, get hit, get miss, update | ns/op, instructions/op (must stay flat), misses/op, TLB misses/op |
| 2 | What does the row table cost? | 3 row sizes × the same key counts, dense keys | two-level minus flat |
| 3 | What do on-demand rows save? | 10M and 100M; 3 row sizes; dense / clustered (runs of ~1,000) / sparse | memory per key next to ns/op |
| 4 | Do huge pages still help at 6 GB? | flat and best two-level; 10M, 50M, 100M; 4 KiB vs 2 MiB | dTLB misses, ns/op |
| 5 | Do baselines hold up at scale? | hash at all sizes; skip list up to 10–25M; B-skiplist artifact at 100M | same metrics |
| 6 | (later) Threads at scale | seqlock on the best layout, 100M, 1–12 threads, uniform and zipf | throughput, latency |

Order: check RAM → 1 → 2 → 3 → 4 and 5 → 6.

### 12.4 Caveats

1. **RAM is the hard limit.** Check `free -g`, and run one structure at a time.
2. **Huge pages at this size need boot-time reservation** (`hugepages=3500` for about 6.4 GB). Otherwise use 4 KiB
   pages.
3. **Huge pages have a TLB limit too.** About 2,000 entries for 2 MiB pages cover about 4 GB, so TLB misses return
   above about 75M keys. Only 1 GiB pages fix that.
4. **The row table cannot be pinned in a cache.** Under random access to GBs of data, the data pushes it out.
   Measure where it lives; don't assume.
5. **Counters cannot separate table misses from Node misses.** The table's cost is "two-level minus flat", which is
   why experiment 1 is needed.
6. **Time per run grows.** GB-sized allocation, clearing and shuffling take seconds. Use 3 repetitions at the top
   sizes; the skip list at 100M would take hours.
7. **Key lists use GBs.** Better: generate keys on the fly from a reversible formula.
8. **Results become DRAM-bound from 10M keys.** Instructions per op remain the O(1) evidence.
9. **The 27-bit key layout change** requires re-running the old 1M tests.
10. **One machine.** Watch for thermal throttling; pin threads; fix the CPU frequency.

---

## 13. Paper readiness

**Not ready for a full VLDB research paper.** The reasons:
- the central idea (VDC placement) is contradicted by the measurements;
- O(1) comes from direct addressing, which reviewers will call a textbook direct-address table;
- integer keys only, 1M keys, no versions, scans or engine integration;
- self-written baselines and one machine.

**Viable paths:**
1. **Workshop paper now** (DaMoN or ADMS):
   - an honest empirical study of bit-reversal placement in a direct-addressed memtable;
   - cache-set aliasing, one line per operation, concurrency designs;
   - the negative result about scatter.
   - Needs: the published baselines run on the same machine, and a second machine.
2. **Full paper later**, reframed:
   - "an O(1), one-line-per-operation, sorted memtable for dense-key workloads", e.g. block stores, where keys are
     block addresses and RASK reports 65–81% of writes come in consecutive runs;
   - the two-level layout (section 12);
   - RocksDB or block-store integration with versions;
   - real traces;
   - published baselines on the same machine;
   - 16+ cores.
3. **Keep VDC as the thesis:** only if a clear win appears. The only candidate so far is hot-key writes, which are
   modest and unexplained.

---

## 14. File map

| File | Contents |
|---|---|
| `DESIGN.md` | original idea, revised model, engineering notes |
| `THINGS_TO_THINK_ABOUT.md` | 13 sections of open problems |
| `memory and cache related problems.md` | 30 memory / cache problems with status and review |
| `CONCURRENCY.md` | concurrent designs and test plan |
| `paper comparison.md` | detailed comparison with the papers |
| `SCALE_EXPERIMENTS.md` | scale experiments (1M–100M keys): how to run, memory explained |
| `README.md` | build and run guide |
| `TODO.md` | progress checklist |
| `results/20261122-081705/analysis.md` | single-threaded analysis (physical machine) |
| `results/mt-20261123-183654/analysis.md` | multithreaded analysis |
| `results/*/report_*.md`, `*.csv` | raw reports and data |

Raw data of the Azure VM run was replaced by the physical run. Its numbers are kept only in section 8.1.
