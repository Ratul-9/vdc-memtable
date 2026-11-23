# Analysis of the huge-page scale run (scale-20261123-210146)

Experiment 4:
- flat, rows-64K and rows-2M on 2 MiB hugetlb pages;
- dense keys, 10M / 50M / 100M;
- 3 repetitions.

Base-page numbers for comparison come from the full base run `scale-20261123-203936` (experiment 2). ns per
operation.

## 1. Checks

| Check | Result |
|---|---|
| Unit tests / correctness / page faults | 10/10 / ✅ / ✅ 0 |
| Every size ran (huge-page pool large enough) | ✅ 9 of 9 |
| Peak memory | 5.96 GiB (budget 9 GiB) |
| O(1): flat, rows-64K, rows-2M | ✅ 1.00 for all |

## 2. Huge pages remove the slowdown at scale

**Random get_hit:**

| keys | flat base → huge | rows-2M base → huge | rows-64K base → huge |
|---:|---|---|---|
| 10M | 14.2 → **12.3** (−14%) | 19.1 → **16.9** (−12%) | 20.7 → 18.0 (−13%) |
| 50M | 15.1 → **12.4** (−18%) | 20.1 → **17.2** (−14%) | 21.8 → 18.2 (−16%) |
| 100M | 19.3 → **12.6** (−35%) | 24.3 → **17.6** (−28%) | 26.3 → 18.9 (−28%) |

- On base pages, flat gets slowed 36% from 10M to 100M keys (14.2 → 19.3). **On huge pages they slow 2% (12.3 →
  12.6).** The same holds for rows-2M (16.9 → 17.6, +4%).
- So **the slowdown on base pages was the page-table walk.** With huge pages the time per get is almost independent of
  size up to 100M keys (6 GB).

**Why, from the counters** (flat, random get):

| keys | dTLB misses/op, base → huge | L1D misses/op, base → huge | LLC misses/op, base → huge |
|---:|---|---|---|
| 10M | 0.99 → 0.00 | 2.00 → 1.01 | 2.02 → 1.97 |
| 50M | 1.00 → 0.46 | 2.36 → 1.23 | 2.06 → 1.99 |
| 100M | 1.00 → 0.74 | 2.61 → 1.45 | 2.53 → 1.99 |

- **TLB misses come back above about 1.6 GB of data, as predicted.** At 50M keys (3 GB) 46% of gets miss the TLB;
  at 100M keys (6 GB), 74%. The TLB reach for 2 MiB pages measures about 1.6 GB on this CPU.
- **But a huge-page TLB miss is cheap.** The page tables for 6 GB of huge pages are only about 3,000 entries (24
  KB), which stay in cache. So the walks no longer miss the LLC (1.99 per get at every size, the same as the data
  line plus the neighbouring line fetched by the hardware prefetcher). L1D misses drop by about 1.1 per get.

**Inserts and updates (random):**

| keys | base → huge |
|---:|---|
| 10M | 13.0 → 12.7 |
| 50M | 13.4 → 12.9 |
| 100M | 14.0 → 12.9 |

The same for rows-64K and rows-2M. Writes were already nearly flat; on huge pages they are flat (12.7–13.0 ns).

**Unchanged by page size:**
- sequential operations: get 4.4 ns (flat) / 4.8 ns (rows), insert 7.8–7.9 ns, update 8.1–8.3 ns;
- misses on rows: 0.8–1.0 ns, because they never touch data.

## 3. The cost of the row table on huge pages

| keys | flat | rows-2M | extra | rows-64K | extra |
|---:|---:|---:|---:|---:|---:|
| 10M | 12.3 | 16.9 | +4.6 ns (+37%) | 18.0 | +5.7 |
| 50M | 12.4 | 17.2 | +4.8 ns (+39%) | 18.2 | +5.8 |
| 100M | 12.6 | 17.6 | +5.0 ns (+40%) | 18.9 | +6.3 |

- **A constant +4.6 to +5.0 ns for rows-2M**, and no extra LLC misses.
- In relative terms the row table now looks more expensive (+40%), because flat got faster. In absolute terms it
  stayed about 5 ns.
- **Inserts and updates: no extra cost** at any size.

## 4. Final picture of the scale experiments (base + huge)

| 100M keys, random | flat (huge pages) | rows-2M (huge pages) | hash table (base) | skip list |
|---|---:|---:|---:|---:|
| get_hit | 12.6 | 17.6 | 32.6 | ~1,500 at 10M |
| insert | 12.9 | 12.9 | 79.2 | ~1,300 at 10M |
| get_miss | 5.7 (base) | 0.8 | 205.1 | – |
| instructions per get | 57.1 at every size | 73.1 at every size | 106–114, varies | 365–422, grows |

1. **O(1) holds from 1M to 100M keys** for flat and rows-2M: identical instruction counts at every size, on both page
   sizes.
2. **With huge pages the time per operation is also nearly constant:** flat gets +2% and rows-2M +4% from 10M to
   100M keys. That's 10× more data with almost no slowdown.
3. **rows-2M:**
   - +5 ns on random gets, which buys sorted order and memory that grows with the data;
   - no extra cost on inserts or updates;
   - misses in 0.8 ns.
4. **The hash table is 1.9–2.6× slower on gets and 6× slower on inserts at 100M.** Its misses are about 250× slower
   than rows-2M's.
5. **Huge pages should be the default** for any Space above about 10M keys. They are worth 14–35% on gets.

Not covered here: huge pages for the hash table at 100M (an 8 GiB pool would exceed the budget), and multiple
threads at 100M keys (experiment 6, not built).
