# Analysis of the quick scale run after the measurement fix (scale-20261123-202607)

Machine: i5-12400F, 18 MiB L3, 16 GB RAM.
- Quick mode: keys 1M, 10M, 25M; density and huge pages at 10M only.
- 3 repetitions, so every counter group was measured.
- Key generation now happens outside the measured sections (the fix after `scale-20261123-202607`'s predecessor,
  `scale-20261123-200631`).

## 1. Checks: everything passes as it should

| Check | Result |
|---|---|
| Unit tests | 10/10, build without warnings |
| Correctness, all 5 experiments | ✅ |
| Page faults during measurements | ✅ 0 |
| Peak memory | E1 1.49, E2 1.50, **E3 8.00**, E4 0.60, E5 2.00 GiB (budget 9) |
| **O(1), flat and rows-4K / 64K / 2M** | ✅ **1.00**: identical instructions per operation at 1M, 10M and 25M, random and sequential order |
| O(1), hash table | ❌ 2.02 (misses: probe chains grow with load) |
| O(1), skip list | ❌ 1.66 |
| TLB counters | now measured (base pages 0.99 per random op; huge pages 0.000) |

**The fix worked.**
- Before the fix, flat get_hit was 80.8 / 90.3 / 85.3 instructions; now it is 57.1 / 57.1 / 57.1.
- Before the fix, the measured times included 9–24 ns of key computation; now they don't. Flat get_hit at 10M went
  from 23.9 to 13.7 ns.

## 2. Results (random order, dense keys, 10M keys unless stated; ns per operation)

### 2.1 Flat array scales flat

| keys | insert | get_hit | get_miss | update |
|---:|---:|---:|---:|---:|
| 1M | 11.1 | 12.9 | 11.4 | 12.5 |
| 10M | 13.0 | 13.7 | 8.1 | 13.3 |
| 25M | 13.2 | 13.9 | 7.4 | 13.5 |

- From 1M to 25M keys (64 MB → 1.6 GB of data), random gets slow down by only 8%.
- Sequential gets cost 4.4 ns at every size.

### 2.2 The row table costs about 5 ns on random gets, nothing on inserts

| | instructions | get_hit, base | get_hit, huge pages | insert | get_miss |
|---|---:|---:|---:|---:|---:|
| flat | 57.1 | 13.7 | 12.3 | 13.0 | 8.1 |
| rows-2M (table in L1) | 73.1 | 18.5 (+4.8) | 16.2 (+3.9) | 13.0 | **0.9** |
| rows-64K (table in L2) | 73.1 | 20.2 (+6.5) | 16.4 (+4.1) | 13.0 | 1.0 |
| rows-4K (table in L3) | 73.1 | 21.0 (+7.3) | – | 13.4 | 1.4 |

- **The row lookup is 16 extra instructions** on a get and 14 on an insert, the same at every size.
- **Where the table lives** (L1D misses per get, on huge pages so page walks don't add noise):
  - flat 1.01;
  - rows-2M 1.01: the table really stays in L1;
  - rows-64K 1.63: the table lives in L2.
- **The table never misses the LLC.** Its cost is the extra dependent load and the extra instructions, which delay
  the start of the data miss.
- **Gets: about +30%.** With 2 MiB rows on huge pages, 12.3 → 16.2 ns.
- **Inserts: no cost.** The table lookup hits L1 and stores don't wait.
- **Sequential gets: +0.4 ns.**
- **Misses: rows are 9× faster.** An absent key has no row, so the table answers in 0.9 ns without touching data.
  The flat array reads an untouched page for each miss (1 TLB miss per op, 8.1 ns).

### 2.3 Huge pages: TLB misses gone, about 10–19% faster

| 10M, get_hit | base | hugetlb | base dTLB/op | hugetlb dTLB/op |
|---|---:|---:|---:|---:|
| flat | 13.7 | 12.3 (−10%) | 0.99 | 0.000 |
| rows-2M | 18.5 | 16.2 (−12%) | 0.99 | 0.000 |
| rows-64K | 20.2 | 16.4 (−19%) | 0.99 | 0.000 |

L1D misses per get drop from about 2.0 to 1.0: the page-table loads disappear.

### 2.4 Key density: memory decides the layout (10M keys)

| pattern | flat | rows-4K | rows-64K | rows-2M |
|---|---|---|---|---|
| dense | 64 B/key, 14.2 ns | 65 B, 20.9 | 64 B, 20.2 | 64 B, 18.6 |
| clustered (runs of 1,000) | 67 B, 14.8 | 68 B, 22.2 | 124 B, 20.8 | **799 B**, 19.4 |
| sparse | 853 B, 20.8 | 854 B, 33.9 | 859 B, 28.1 | 859 B, 29.3 |

- **Clustered keys are as fast as dense** (14.8 vs 14.2 ns for flat). The earlier "clustered is 3× slower" was the
  key generator.
- **Clustered keys need small rows:** 2 MiB rows waste 12.5× the memory.
- **Sparse keys cost 13× the memory in every layout,** and are 46% slower for flat (20.8 vs 14.2 ns). With data
  spread over 8 GiB, even the page-table walks miss the cache (LLC 2.8 per get).
- **rows-4K suffers on sparse keys** (33.9 ns, 1.6 TLB misses per get): its 8 MiB table also needs a TLB entry per
  lookup on base pages.
- **The flat array on base pages already gets on-demand memory** from the operating system at 4 KiB granularity, so
  rows only save memory over flat when rows are smaller than a page. That doesn't happen here: rows-4K = flat.

### 2.5 Baselines

| get_hit | flat | rows-2M | hash table | skip list |
|---|---:|---:|---:|---:|
| 1M | 12.9 | 16.4 | 30.9 | 730 |
| 10M | 13.7 | 18.5 | 45.2 | 1,500 |
| 25M | 13.9 | 19.0 | 50.2 | – |

| 10M keys | flat | hash table | skip list |
|---|---:|---:|---:|
| LLC misses per get | 2.0 | 8.5 | 62 |
| dTLB misses per get | 0.99 | 1.04 | 29 |
| insert ns | 13.0 | 42.3 | 1,303 |
| get_miss ns | 8.1 (rows: 0.9) | 105 (181 at 25M) | 46 |

- Gets: the hash table is **3.3–3.6×** and the skip list **about 110×** slower than flat.
- **Misses: rows are about 117× faster than the hash table** at 10M keys (0.9 vs 105 ns). The hash table's misses get
  worse as it fills.

## 3. What this means

1. **O(1) is now measured cleanly up to 25M keys** for both layouts: identical instruction counts at every size.
2. **The two-level layout is O(1), sorted, and nearly as fast as flat.** Inserts cost the same, gets cost about 30%
   more (4–5 ns), and misses are 9× faster. With 2 MiB rows its table provably stays in L1.
3. **Memory only goes down with on-demand rows when keys are grouped, and the rows must then be small.** For sparse
   keys nothing helps. This is the honest limit of the design.
4. **Measurement quality is now good enough for the full run.**

## 4. Next

Run the full experiment (`--phase base`, then `--phase huge`). It adds 50M, 75M and 100M keys, where the data
(up to 6 GB) exceeds what the TLB can cover even with huge pages.
