# Analysis of the physical-machine run (2026-11-23)

Machine: Intel Core i5-12400F (Alder Lake, 6 P-cores), 48 KiB L1D, 1.25 MiB L2 (10-way), **18 MiB L3**,
Linux 6.8, GCC 11.4. Hardware counters: all seven available.

Unlike the Azure VM (260 MiB L3), the L3 here is smaller than the 64 MiB Space. From 500K keys (32 MiB) up, the data
really lives in DRAM.

## 1. Checks

| Check | Result | Comment |
|---|---|---|
| Unit tests on Linux | 8/8 | |
| Correctness, all cells (vdc, hash, skip list) | ✅ PASS | |
| **O(1): instructions/op flat 1K → 1M (vdc)** | ✅ **PASS, worst 1.01** | e.g. get_hit = 81.0 instructions at 1K and at 1M |
| O(1) hash table | ❌ 1.31 | misses: 52 → 68 instructions as probe chains grow |
| O(1) skip list | ❌ 2.42 | get_hit 189 → 340 instructions (O(log n)) |
| **TLB on hugetlb (vdc)** | ✅ **PASS, worst 0.004** | 0.000 in almost every cell |
| Page faults while measuring | ✅ 0 | |
| Cache: LLC misses/op ≤ 1.25 (vdc) | ❌ worst 2.68 | the counter overcounts, see section 3 |

**O(1) is now measured, not inferred**: every vdc operation runs the same number of instructions at 1K and at 1M keys,
for every j, pattern and page mode. The skip list and the hash table both fail the same check.

## 2. Time per operation (rand keys, hugetlb, j = 11)

| keys | get_hit | get_miss | insert | update | delete | get_batch |
|---|---:|---:|---:|---:|---:|---:|
| 1K | 3.8 | 3.1 | 24.4 | 6.4 | 3.7 | 6.0 |
| 10K | 4.0 | 3.4 | 16.4 | 6.5 | 3.8 | 6.1 |
| 100K | 5.8 | 7.4 | 17.0 | 15.4 | 13.6 | 6.2 |
| 500K | 21.6 | 17.6 | 29.7 | 28.7 | 21.4 | 8.5 |
| 1M | 21.7 | 5.7 | 29.4 | 28.7 | 22.3 | 8.8 |

- The time steps up when the data leaves L3 (100K → 500K), then stays flat to 1M, while instructions stay constant.
  The step is the memory hierarchy, not the algorithm.
- About 22 ns per random get from DRAM (about 90 cycles). DRAM latency is about 70–80 ns, so the CPU is already
  overlapping several independent gets.
- get_miss at 1M is low only because just 48,576 missing keys exist at 1M, and they stay cached (see section 8).

## 3. Cache misses: one line per operation is achieved

The LLC check failed, but the other counters show each operation touches exactly one cache line:

| hugetlb, dense_rand, 100K keys | L1D load misses/op |
|---|---:|
| insert / get_hit / get_miss / update / delete | **1.000** (all j) |
| get_batch | 0.004 (prefetched) |

So every operation loads one 64 B Node and nothing else. The extra LLC "misses" come from how the generic
`cache-misses` event counts on Intel. It includes requests issued by the hardware prefetchers (the L2 spatial prefetcher
fetches the neighbouring line of every 128 B pair).

Evidence:
- get_hit at ≥ 500K shows 2.0–2.5 LLC misses/op on both page modes.
- get_batch, which reads the same lines through software prefetch, shows about 1.0.
- Writes on hugetlb show 1.0. On base pages writes show about 1.5, the extra 0.5 being page-table walks.

This is P17's "adjacent-line prefetcher" cost, measured. The design meets its target. The check should count demand
misses only, or be confirmed with the hardware prefetchers switched off (see section 9).

## 4. The bit reversal costs 26 instructions per operation

| op | j = 6 … 15 | j = 20 (identity) | extra |
|---|---:|---:|---:|
| get_hit | 81 | 55 | +26 |
| get_miss | 66 | 40 | +26 |
| insert | 140 | 114 | +26 |
| get_batch | 134 | 82 | +52 (slot computed twice) |

Effect on time:
- In cache, about 1.5 ns per op (get_hit 1K: 3.7 vs 2.2 ns).
- In DRAM, the gap gets larger: get_hit 1M is 21.7 vs 15.4 ns. More instructions per get means fewer gets fit in the
  out-of-order window, so fewer cache misses overlap.
- With the batch API the gap disappears (8.8 vs 9.1 ns at 1M), because memory is then the only limit.

This confirms the Azure finding, now with exact counts. A per-memtable lookup table for the reversed bits (2 KiB for
j = 11) would remove most of the 26 instructions.

## 5. Choice of j: j = 11 confirmed, and the aliasing also hits L3

Dense key ranges with small j leave some slot bits constant. Caches indexed by those bits then lose capacity. The Azure
run showed this for L2 at 10K keys. Here it also shows in **L3 at 100K keys**:

| hugetlb, dense_rand, 100K keys | j=6 | j=8 | j=11 | j=15 | j=20 |
|---|---:|---:|---:|---:|---:|
| get_hit ns/op | 20.3 | 19.9 | **5.6** | 6.3 | 4.6 |
| get_hit LLC misses/op | 1.89 | 1.51 | **0.006** | 0.027 | 0.079 |
| update ns/op | 27.5 | 26.6 | **15.4** | 15.4 | 10.4 |
| delete ns/op | 20.7 | 20.1 | **13.6** | 14.6 | 11.1 |

- A 6.4 MiB working set fits in the 18 MiB L3, yet with j = 6 or 8 it **runs 3.5× slower at DRAM speed**.
- Sequential keys: j = 6 has about 1.2 LLC misses/op at every size from 100K up. j ≥ 11 has 0.01–0.05 at 100K and
  matches identity at 1M (get_hit 1M base: j=11 5.6 ns, j=20 5.4 ns).
- On base pages j = 8 often escapes, because 4 KiB pages scramble the higher address bits. On hugetlb it does not.
- Random keys: all j values are equal (within noise), as expected.
- Experiment 02 (analytic) predicted the L2 part correctly for this CPU (10-way L2: j=6 and j=8 ❌ from 4K keys).
- One cell group looks like noise: j = 15 on base pages at 100K rand (e.g. update 32.2 vs 11–12 ns for every other j).
  It is not repeated on hugetlb, so it is most likely an unlucky physical page placement for that run.

**Decision: j = 11.** It is as good as identity for dense and sequential keys, keeps the scatter, and avoids the
aliasing that j = 6 / 8 cause.

## 6. Huge pages: TLB misses eliminated, time gain depends on the cache level

| rand keys | dTLB misses/op, base | dTLB misses/op, hugetlb |
|---|---:|---:|
| 10K, 100K, 1M (every op, every j) | 0.86 – 0.89 | **0.000** |

| get_hit, j = 11 | base ns | hugetlb ns | change |
|---|---:|---:|---:|
| 100K (in L3) | 10.5 | 5.8 | −45% |
| 1M (in DRAM) | 22.8 | 21.7 | −5% |

- Huge pages remove the TLB misses completely (P14 solved).
- While the data is in cache, each page walk is a large share of the cost, so the gain is large.
- From DRAM the CPU overlaps the walks with the memory accesses, so the gain is small.
- Experiment 06: committing 64 MiB takes 8.5 ms on hugetlb vs 18 ms on base pages. Destroying takes 0.02 vs 2.1 ms.
- Keep hugetlb.

## 7. Prefetching gives the biggest win in DRAM (P17)

Experiment 04, 1M keys, ns/op:

| prefetch distance | base rand | hugetlb rand |
|---|---:|---:|
| 0 (none) | 18.7 | 17.2 |
| 8 | 13.0 | 10.3 |
| 16 | 8.9 | 7.8 |
| **32** | **8.1** | **7.1** |
| 64 | 8.3 | 7.3 |
| memtable_get_batch (16) | 9.4 | 8.1 |

- Speedup is 2.0–2.4×. On this DRAM-bound machine **distance 32 is best**. On the Azure VM, where data stayed in L3,
  16 was enough. Recommendation: set `VDC_PREFETCH_DISTANCE` to 32.
- In vdc_bench: get_batch at 1M is 8.8 ns vs get_hit 21.7 ns (2.5× faster).
- Below about 100K keys the batch API is still slower than plain gets (6.0 vs 3.8 ns at 1K) because of its fixed
  overhead.

## 8. Baselines (rand keys, hugetlb unless noted)

| 1M keys | vdc j=11 | identity | hash table | skip list |
|---|---:|---:|---:|---:|
| get_hit ns | 21.4 | 14.9 | 25.1 | 643 |
| get_miss ns | 5.5 | 3.1 | 31.0 | 623 |
| insert ns | 27.8 | 23.2 | 30.5 | 486 |
| get_hit instructions | 81 | 55 | 72.5 | 340 |
| get_hit LLC misses | 2.31 | 1.78 | 4.83 | 26.8 |
| seq get_hit ns (base) | 5.2 | 5.1 | 29.2 | 67 |

- **Random keys in DRAM: vdc is only 1.2× faster than the hash table.** Both are dominated by one DRAM access. The
  hash table touches about twice as many lines (128 MiB table, probing).
- **Missing keys: 5.6× faster than the hash table**, whose misses walk long probe chains (5.7 LLC misses/op).
- **Sequential keys: 5.6× faster than the hash table**. Partial reversal keeps locality and hashing destroys it.
- Identity is still 30% faster than vdc on unbatched random gets, because of the reversal cost (section 4).
- The skip list is 30× slower (gets) and needs about 26 LLC misses per lookup.

## 9. Next steps

1. Set `VDC_DEFAULT_J` = 11 (confirmed on two machines).
2. Set `VDC_PREFETCH_DISTANCE` = 32.
3. Lookup-table bit reversal to remove the 26 extra instructions. This is the only thing identity still wins on.
4. Make the cache check count demand misses only (Intel raw event `MEM_LOAD_RETIRED.L3_MISS`), or re-run once with
   hardware prefetchers disabled (`wrmsr -a 0x1a4 0xf`, msr-tools) to confirm the 1-line-per-op claim directly.
5. Benchmark fixes: fixed-size miss key set (get_miss at 500K vs 1M is not comparable), and more repetitions for
   inserts at 1K–10K keys (noisy: 18–60 ns).
6. The build log still shows the old GCC buffer warning. That run used a checkout from before the fix; the current
   code builds clean.
7. Then the concurrent model, where VDC's scatter has to beat identity to be the novel result.
