# VDC-Memtable

A memtable where the slot of a key is the Van der Corput (bit-reversal) number of
the key: no hashing, no probing, no collisions, O(1) worst-case put / get /
update / delete. Single-threaded research prototype in C.

- Keys: integers 0 .. 2^20 - 1 (one memtable = one VDC-Space of 2^20 slots).
- Slot: `rev_{20-j}(key >> j) << j | (key & (2^j - 1))`; j low key bits stay in place (j = 0 full reversal, j = 20 identity).
- Node: 64 B = one cache line (3 B key, 5 B reserved, seqno, ext_off, val_len, flags, 32 B inline value).
- Space: 2^20 x 64 B = 64 MiB, on base pages or on 2 MiB hugetlb pages.

Design notes: [DESIGN.md](DESIGN.md), open problems: [THINGS_TO_THINK_ABOUT.md](THINGS_TO_THINK_ABOUT.md)
and [memory and cache related problems.md](memory%20and%20cache%20related%20problems.md), progress: [TODO.md](TODO.md).

## Layout

| Path | Contents |
|---|---|
| `include/vdc/` | `vdc.h` (key → slot), `slot.h` (Node), `vdc_space.h`, `arena.h`, `memtable.h`, `bitmap.h`, `platform.h`, `config.h`; concurrent: `concurrent.h`, `queue.h`, `service.h` |
| `src/` | library sources (`platform_linux.c` on Linux, `platform_posix.c` elsewhere) |
| `tests/` | unit tests (`make test`) |
| `baselines/` | hash table (linear probing) and skip list with the same API semantics |
| `bench/` | `vdc_bench` driver: timings + hardware counters, markdown tables, CSV |
| `bench_scale/`, `include/dat/`, `src/dat/` | scale experiments `dat_scale` and the two-level layout |
| `bench_mt/` | `vdc_mtbench` multithreaded driver (11 designs, threads, workloads, latency) |
| `experiments/` | focused experiments 01–07, each with a README |
| `scripts/` | Linux setup and run scripts |
| `results/` | benchmark output (one folder per run) |

## Build

```bash
make            # library, test runner, vdc_bench, experiments (nothing is run)
make test       # unit tests (also work on macOS)
make debug      # unit tests with -O0, assertions, ASan + UBSan
make clean
```

## Running the benchmarks on Linux

Use a bare-metal Linux machine (x86-64 or ARM64) with gcc or clang and make. Virtual machines often hide
the hardware counters. Steps:

1. Reserve 2 MiB huge pages (once per boot):

   ```bash
   sudo scripts/setup_hugepages.sh 128
   ```

2. Allow hardware counters for your own process (needed if `perf_event_paranoid` > 2):

   ```bash
   scripts/perf_counters.sh
   ```

   ```bash
   sudo scripts/perf_counters.sh --enable
   ```

3. Optional, for stable numbers: fixed CPU frequency and an idle core.

   ```bash
   sudo cpupower frequency-set -g performance
   ```

4. Run everything (build, unit tests, benchmarks, experiments):

   ```bash
   scripts/run_bench.sh --cpu 2
   ```

   The full run should take very roughly 30–60 minutes (not measured yet). `--quick` runs 1K / 100K / 1M keys with
   3 repetitions and is much shorter. `--skip-hugetlb` runs base pages only.

5. Read `results/<timestamp>/report_vdc.md`, starting with the **Checks** table.

| Output file | What it shows |
|---|---|
| `unit_tests.txt` | unit test results on the Linux machine |
| `report_vdc.md`, `results_vdc.csv` | main matrix: j = 6, 8, 11, 15, 20 × keys 1K, 10K, 100K, 500K, 1M × 3 patterns × 6 operations × base / hugetlb pages |
| `report_baselines.md`, `results_baselines.csv` | vdc (j = 6, 11, 20) vs hash table vs skip list |
| `exp01_bijection.md` | every j is a bijection; pages touched per key range |
| `exp02_aliasing.md` | cache-set aliasing per j (analytic, uses this machine's cache geometry) |
| `exp04_prefetch.md` | prefetch distance sweep for batched gets |
| `exp05_layout.md` | Node layout on this machine |
| `exp06_fill.md` | create / fill / clear / destroy cost, page faults, resident memory |

Rebuild pivot tables from a CSV, for example for one metric or to compare two runs:

```bash
scripts/plot_results.py results/<run>/results_vdc.csv --metric llc_miss_op --op get_hit
```

## Multithreaded benchmarks

Eleven concurrent designs: direct access with a global mutex, striped spinlocks, striped reader-writer locks, a
per-node lock or a seqlock (lock-free reads), and delegation through lock-free queues to key-owning consumer threads
(MPSC with one owner, sharded MPSC queues, one shared MPMC queue), each with synchronous or asynchronous producers.
Design and test plan: [CONCURRENCY.md](CONCURRENCY.md).

```bash
scripts/run_mt_bench.sh
```

Output: `results/mt-<timestamp>/report_mt.md` (Checks, fastest design, scaling, VDC scatter vs identity, throughput
and p50 / p99 / p99.9 latency per workload) and `results_mt.csv`. `make tsan` runs the unit tests under
ThreadSanitizer.

## Scale experiments (1M – 100M keys)

Flat array vs a two-level layout with rows created on demand, plus the hash table and skip list, from 1M to 100M keys,
never above a 9 GiB memory budget. Guide, including what happens in memory: [SCALE_EXPERIMENTS.md](SCALE_EXPERIMENTS.md).

```bash
scripts/scale/run_scale.sh --quick
```

## What is measured

**Patterns**: `seq` = keys 0..N-1 ascending; `dense_rand` = keys 0..N-1 in random order; `rand` = N random keys from
the whole domain in random order.

**Operations**: `insert` (N new keys into an empty table), `get_hit`, `get_miss` (never-written keys), `get_batch`
(get_hit through the prefetching `memtable_get_batch`), `update` (overwrite), `delete` (tombstone).

**Metrics per operation** (medians): ns/op, instructions/op, cycles/op, L1D load misses/op, LLC misses/op,
dTLB load + store misses/op, page faults per run.

**How each claim is checked**

| Claim | Evidence in the report | Target |
|---|---|---|
| Operations are O(1) | instructions/op from 1K to 1M keys (Checks + "O(1) detail") | max/min ≤ 1.10 for vdc; the skip list is expected to fail (O(log n)) |
| Cache misses minimised | LLC misses/op | ≤ 1.25 (one 64 B Node = one line); `seq` and small N near 0 |
| TLB misses minimised | dTLB misses/op, base vs hugetlb ("Huge pages vs base pages") | ≤ 0.05 on hugetlb |
| No page faults while measuring | page faults per run | 0 (memory is populated) |
| Correct results | every operation's status checked | all ok |
| Effect of j | every table has one column per j; "Fastest variant per workload" | pick j from these |

Time per operation **is expected to grow** with N: 1K keys fit in L1, 1M keys (64 MiB) live in DRAM. That is the
memory hierarchy, not the algorithm, which is why O(1) is judged by instructions per operation, and the miss
columns explain the time.

**Expected values** (to compare against tomorrow's numbers):

- instructions/op: the same at every N for vdc (about 20–40 per get, depending on the compiler).
- `rand` get at 1M keys: about 1 LLC miss/op (DRAM latency, roughly 70–120 ns on base pages).
- dTLB misses at 1M keys, `rand`: close to 1 per op on 4 KiB pages, close to 0 on hugetlb.
- `seq`: few misses for every j ≥ 6, because 2^j consecutive keys are contiguous and the prefetcher streams them.
- j = 20 (identity) is the baseline for "does the VDC permutation cost anything".
- `get_batch` should beat `get_hit` at large N with random keys (overlapping misses).

## vdc_bench options

```text
--impl LIST       vdc,hash,skiplist                          (default vdc)
--j LIST          unreversed low key bits for vdc            (default 6,8,11,15,20)
--keys LIST       key counts, K = 1e3, M = 1e6, max 1048576  (default 1K,10K,100K,500K,1M)
--patterns LIST   seq,dense_rand,rand                        (default all)
--ops LIST        insert,get_hit,get_miss,get_batch,update,delete (default all)
--pages LIST      base,hugetlb                               (default base)
--reps N          repetitions per cell and counter set       (default 5)
--min-ops N       minimum ops per read/update sample         (default 1M)
--no-perf         timings only
--flush-mb N      write N MiB between repetitions to evict caches
--csv FILE        append results to FILE
--seed N          RNG seed                                   (default 42)
```

## Decisions these tests use

- Single-threaded; concurrency, NUMA and latency tails are deferred.
- Key counts 1K, 10K, 100K, 500K, 1M (2M skipped: one memtable holds 2^20 keys).
- j = 6, 8, 11, 15, 20 (20 = identity baseline).
- 32 B inline values only (arena not exercised by the benchmarks).
- Page modes: base pages (THP disabled per mapping) and the reserved 2 MiB hugetlb pool with MAP_POPULATE;
  a missing pool is an error, not a silent fallback.
- Software prefetch through the batch API (distance 16, swept in experiment 04).
- The occupancy bitmap exists (`vdc/bitmap.h`) but is not updated by the memtable.
- Range scans and flush are not benchmarked yet.
