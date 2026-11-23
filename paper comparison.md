# VDC memtable vs published results

Compares my measured results (physical run, 2026-11-23: i5-12400F, 18 MiB L3, single thread, standalone
memtable, 1K–1M dense integer keys, 32 B values) with the papers in `paper-results-summary.md`.

All paper numbers are the authors' claims. Absolute numbers are not comparable across machines, so this file
compares **ratios against each paper's own baseline**, **per-operation counters**, and, where a paper gives
single-thread numbers, absolute times.

## 1. Rules used to compare single-threaded with multi-threaded results

| Problem | How it is handled here |
|---|---|
| Papers use 16–160 threads, I use 1 | Compare speedups over each paper's own baseline (a ratio survives thread count better than throughput). Where only multi-thread throughput exists, divide by **physical cores** (not hyper-threads) to get a per-core rate. Contention, NUMA and locks are then still inside their number but not mine, which favours me. |
| Different data sizes (100M elements vs my 1M) | Only my 1M row is compared: it is the only one that lives in DRAM (64 MiB > 18 MiB L3), as their data does. |
| Standalone index vs inside RocksDB | Two separate groups below. Inside RocksDB the memtable is only part of each operation (Amdahl), so in-RocksDB gains are naturally 1.2–1.8×, not 10×+. |
| Different keys | Mine: 20-bit dense integers, no comparisons. Theirs: 8 B integers (B-skiplist, Foresight) or 16–20 B strings (RocksDB papers). This is the largest unfairness and is listed as a threat in section 7. |

## 2. Sanity check: is my skip list baseline realistic?

If my skip list were unusually slow, every "×" against it would be inflated. It is in line with published skip lists:

| Measure | My skip list (1M keys) | Published | Source |
|---|---:|---:|---|
| LLC misses per op | 13.4 (insert), 25–27 (get), about 20 averaged | Folly: 24.5 per op (load + YCSB-C, 200M ops) | B-skiplist Table 1, divided by 200M ops |
| Insert time, 1 thread, memtable only | 0.49 µs | RocksDB SkipList: 0.80 µs | Aparajita, `memtablerep_bench` |
| Single-thread read rate | 1.56 Mops/s | Folly 0.36 Mops/s per core, B-skiplist 1.16 per core (128 threads, 100M elements) | B-skiplist |

My skip list is about as cache-hungry as Folly's and somewhat faster than RocksDB's (integer keys, no string
compares, smaller data). So it is a fair, not a strawman, baseline. It is still LevelDB-style; RocksDB's
InlineSkipList and the B-skiplist were not run on my machine.

## 3. Speedup over each paper's own baseline

| Design | Where measured | Baseline | Reads | Writes | Threads |
|---|---|---|---:|---:|---|
| Arctic (OSDI '26) | inside RocksDB | RocksDB SkipList | 1.3× (geomean, YCSB-C vs ART) | up to 1.40× | 80 |
| Aparajita (preprint) | inside RocksDB | RocksDB SkipList | 1.22–1.39× | about 1.0× (1.48–1.52× memtable-only) | 1–64 |
| MountDB (arXiv '26) | inside RocksDB | RocksDB SkipList | up to 1.77× | up to 1.3× | n/a |
| JellyFish (Middleware '20) | inside RocksDB | RocksDB memtable | 1.51× | 1.08× insert, 1.16× update | 16 |
| FloDB (EuroSys '17) | inside LevelDB | HyperLevelDB | (read-only vs LevelDB up to 100×) | 1.9–3.5× | up to 16+ |
| Foresight (arXiv '26) | standalone | same skip list without it | up to 1.45× | | 128 |
| B-skiplist (ICPP '25) | standalone | Folly skip list | 3.2× | 2.1× | 128 |
| **VDC (mine)** | **standalone** | **my skip list** | **30×** (8.8 ns batched: **73×**) | **17.5×** | **1** |
| **VDC (mine)** | standalone | my hash table | 1.17× (misses 5.6×, sequential keys 5.6×) | 1.1× | 1 |

How to read it:
- The only fair row-to-row comparisons are with the other **standalone** designs (Foresight, B-skiplist). Against a
  skip list, VDC's 30× is about 10× larger than the B-skiplist's 3.2×. That is expected and not by itself a result:
  VDC is a direct-addressed array, so it skips the search entirely, and it only handles dense integer keys.
- The more honest standalone comparison is against a hash table, where VDC is only 1.2× faster on random present keys
  (both pay one DRAM access). Its clear wins are absent keys and sequential keys (5.6×), plus a constant worst case.
- The in-RocksDB papers cannot be compared yet: VDC is not inside RocksDB. Section 6 estimates what it could reach.

## 4. Cache misses per operation

| Design | LLC misses per op | Reduction vs its baseline | Source |
|---|---:|---:|---|
| Folly skip list | 24.5 (point), 55 (scans) | (baseline) | B-skiplist Table 1 |
| B-tree (BP-tree) | 10.5 | 2.3× | B-skiplist Table 1 |
| B-skiplist | 7.5 (point), 10 (scans) | 3.3× | B-skiplist Table 1 |
| Foresight | n/a | up to 2× (−50%) | Foresight |
| JellyFish | n/a | 2.3× (−57%) | JellyFish |
| Aparajita | 62 L1 misses per lookup (end-to-end RocksDB Get) vs 104 | 1.7× (L1) | Aparajita |
| My skip list | about 20 (13 insert, 26 get) | (baseline) | my run |
| My hash table | 4.8 (get), 5.7 (miss) | | my run |
| **VDC** | **1.0 L1 miss per op**; LLC counter 1.0 for writes and batched gets, 2.0–2.5 for plain gets (hardware-prefetcher traffic, see analysis.md) | **10–26× vs my skip list** | my run |

One 64 B line per operation is the floor for any index whose data is not already cached. VDC reaches it. This is the
most transferable claim, because it depends on the access pattern, not on thread count or machine. The best
published structure here, the B-skiplist, still needs about 7.5 LLC misses per point operation.

## 5. Single-thread absolute numbers (the only directly comparable ones)

Only Aparajita reports single-thread numbers:

| 1 thread | RocksDB SkipList | Aparajita | VectorRep | VDC (memtable only, 1M keys) |
|---|---:|---:|---:|---:|
| Memtable insert (`memtablerep_bench`) | 0.80 µs | 0.54 µs | | **0.028 µs** |
| db_bench `fillrandom`, end-to-end | 1.75 µs | 1.47 µs | 0.45 µs | not measured |
| db_bench `readrandom`, end-to-end | 1.89 µs | 1.37 µs | 0.9–3.3 s (copy + sort per Get) | not measured |

VDC's memtable insert is about 20–30× faster than the RocksDB and Aparajita memtables, but with integer keys and 32 B
values, against their 16 B string keys and 100 B values. Treat it as an upper bound.

## 6. What VDC could plausibly reach inside RocksDB (estimate, not measured)

Amdahl's law with the Aparajita single-thread numbers:
- **Writes**: VectorRep (an append, nearly free memtable) gets RocksDB to 0.45 µs per insert, 3.9× the SkipList.
  VDC's insert costs about as little as an append, so `fillrandom` should land between the SkipList's 1.75 µs and
  VectorRep's 0.45 µs, i.e. roughly **1.8–3.9× at 1 thread**. Unlike VectorRep, gets stay O(1).
- **Reads**: Aparajita saved 0.53 µs per Get (1.39×). VDC removes at least that and more of the remaining memtable
  cost, so a guess is **1.4–2×** on `readrandom`. That is above the 1.2–1.8× band of Arctic, Aparajita and MountDB.
- **Multi-thread writes**: Aparajita shows 22,000+ instructions per insert in RocksDB's write-thread group, so the
  memtable is not the bottleneck there. Expect a tie, as Aparajita got.

Integration blockers that the estimate ignores:
1. RocksDB stores **internal keys** (user key + sequence number) and needs several versions of one key for snapshots.
   VDC keeps one version per slot. A per-key version list (JellyFish's idea) would be needed.
2. RocksDB keys are byte strings. db_bench happens to generate them from dense integers (0 .. num−1), so a numeric
   decode works for db_bench. Real string keys need the key → dense-id mapping (problem P3).
3. Aparajita's setup uses a 2M key space: that needs 2^21 slots (two Spaces or a wider Space).
4. Memory: with 100 B values VDC needs 64 MiB of Nodes plus about 104 MiB of arena for 1M keys, while a 64 MB
   RocksDB write buffer holds about 480K skip-list entries. VDC only pays off when the Space is well filled.

## 7. Multi-threaded results: what can and cannot be said

Per physical core, read throughput on point workloads:

| Design | Mops/s per core | Setup |
|---|---:|---|
| Folly skip list | 0.36 | 128 threads on 64 cores, 100M elements, 8 B keys |
| B-skiplist | 1.16 | same |
| My skip list | 1.56 | 1 thread, 1M keys |
| VDC, plain gets | 46.7 | 1 thread, 1M keys (DRAM) |
| VDC, batched gets | 113.6 | 1 thread, 1M keys (DRAM) |

- **Do not put the VDC rows next to the B-skiplist in a paper.** Data size (1M vs 100M), keys, locking and NUMA all
  differ. The only safe conclusion is the sanity check in section 2.
- What VDC's design predicts for multiple threads: gets never change the structure and touch one line, and puts
  change one line, so there is no shared search path to lock. Scaling should be limited by memory bandwidth and
  memory-level parallelism, not by synchronisation. Single-thread batched gets (114 Mops/s) are already close to what
  one core can keep in flight. This is a hypothesis for the concurrent version to test.
- The places where multi-threaded VDC can fail: many threads writing the **same** key (true sharing on one line), and
  sequential-key writers on identity mapping (that is where VDC's scatter should beat j = 20).

## 8. Feature comparison (what a reviewer will ask)

| | VDC | B-skiplist | Foresight | Arctic | Aparajita | JellyFish | MountDB | FloDB |
|---|---|---|---|---|---|---|---|---|
| Point op cost | O(1) worst case | O(log_B n) | O(log n) | O(key length) | O(n / 15) per node | O(log n) | learned | O(1) buffer + O(log n) |
| Arbitrary keys | ❌ dense 20-bit ints | ints | ≤ 8 B | ✅ strings | ✅ strings | ✅ | numeric | ✅ |
| Range scans | ❌ not yet (ordered walk possible) | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Multi-version / snapshots | ❌ newest only | ❌ | ❌ | | ✅ (RocksDB) | ✅ | ✅ | ✅ seqnos |
| Concurrency | ❌ not yet | locks | lock-free | lock-free | per-node lock | CAS | | ✅ |
| Inside RocksDB | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | LevelDB |
| Memory per key | 64 B fixed, plus 64 MiB regardless of fill | | +8–24 B | 0.97–1.5× ART | 1.4× skip list | +8 B | | |
| Warm-up cost | 8.5 ms commit (hugetlb) | | | | | | cold start (needs skeleton) | |

Where VDC is genuinely different:
- **The only design with an O(1) worst case and exactly one cache line per operation.** The others reduce misses by
  2–3×; VDC removes the search.
- **Block-store keys are dense integers.** RASK reports that 65–81% of Alibaba writes are runs of consecutive blocks.
  That is the exact case where VDC beats a hash table by 5.6× and where partial reversal keeps locality. This is
  the most natural application to argue for.
- **No cold start** (MountDB's main problem): a Space is ready after committing 64 MiB (8.5 ms on huge pages).
- **Ordered output without sorting** (FloDB must drain and sort; VectorRep copies and sorts on every Get). Not
  measured yet.

## 9. Measurement flaws from the summary, checked against my harness

| Flaw listed in the summary | My harness |
|---|---|
| Batch-averaged latency (B-skiplist) | ✅ not used, but I report medians of throughput only; per-op tail latency is still missing |
| Buffers so large nothing flushes | ⚠ no flush exists yet |
| Thread counts above physical cores | ✅ single thread, pinned |
| One machine, one tool | ❌ one physical machine (plus the Azure VM run) |
| Integer keys only | ❌ yes, by design |
| No memory-per-key reporting | ⚠ fixed 64 MiB per Space is known, not reported per key |
| No hardware counters | ✅ counters on the physical run |

## 10. Experiments that would make the comparison publishable (in order)

1. **Run the published baselines on my machine, single thread, same keys**: the B-skiplist artifact
   (github.com/Ratbuyer/bskip_artifact, which also contains Folly and the B-tree), and RocksDB's InlineSkipList via
   `memtablerep_bench`. This replaces every cross-paper estimate above with measured ratios.
2. **A RocksDB MemTableRep plugin for VDC** (Aparajita shows it can be done without patching RocksDB), decoding
   db_bench's integer keys, with a per-key version list. Then run Aparajita's exact config (`fillrandom`,
   `readrandom`, `readwhilewriting`, 16 B keys / 100 B values) plus a 64 MB buffer with compaction on.
3. **The concurrent version** (1–6 threads on this machine, more on a bigger one), with per-operation tail latency,
   not batch averages.
4. **A block-store trace** (Alibaba / Tencent traces used by RASK), where dense integer keys and runs are natural.
5. Report memory per key and, once flush exists, L0 file counts.
