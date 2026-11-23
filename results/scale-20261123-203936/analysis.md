# Analysis of the full base-page scale run (scale-20261123-203936)

Machine: i5-12400F, 18 MiB L3, 16 GB RAM, 4 KiB pages.
- Experiments 1, 2, 3 and 5; experiment 4 (huge pages) is the next phase.
- Keys 1M–100M, 3 repetitions, key generation outside the measurements.
- Numbers below: dense keys, random order, ns per operation, unless stated.

## 1. Checks

| Check | Result |
|---|---|
| Unit tests / correctness / page faults | 10/10 / ✅ every result correct / ✅ 0 |
| Peak memory | E1 5.96, E2 5.97, E3 8.01, E5 8.00 GiB (budget 9 GiB): ✅ budget held |
| Skips | only the skip list above 10M keys (time limit), as designed |
| **O(1), flat** | ✅ **1.00**: get = 57.1 instructions at every size from 1M to 100M |
| **O(1), rows-2M / rows-4K** | ✅ 1.03 / 1.04 |
| O(1), rows-64K | ❌ 1.36 (see note) |
| O(1), hash table / skip list | ❌ 2.02 / 1.66 |

**Note on rows-64K.** The 1.36 comes only from misses on clustered keys. A miss has two paths:
- no row exists: 16 instructions;
- the row exists but the key is absent: about 25 instructions.

At 100M clustered keys more misses land in rows that exist, so the average moves from 16.7 to 22.8 instructions. Each
path is constant, so the worst case stays fixed and the operation is still O(1). The check compares averages, which
is too strict for a two-path operation. Hits, inserts and dense-key operations are exactly constant (73.1
instructions for every row size at every size).

## 2. The flat array up to 100M keys

| keys | insert | get_hit | get_miss | update | L1D / LLC / dTLB misses per get |
|---:|---:|---:|---:|---:|---|
| 1M | 11.3 | 13.0 | 11.1 | 12.6 | 1.82 / 1.59 / 0.90 |
| 10M | 13.0 | 13.6 | 8.0 | 13.4 | 2.00 / 2.02 / 0.99 |
| 25M | 13.3 | 13.9 | 7.3 | 13.5 | 2.13 / 2.05 / 0.99 |
| 50M | 13.4 | 14.5 | 6.7 | 13.6 | 2.36 / 2.06 / 1.00 |
| 75M | 13.7 | 16.9 | 5.9 | 13.8 | 2.51 / 2.27 / 1.00 |
| 100M | 14.0 | 18.7 | 5.7 | 14.2 | 2.61 / 2.53 / 1.00 |

- **Instructions never change** (57.1 per get). The algorithm is O(1).
- **Inserts and updates are almost flat:** +24% from 1M to 100M.
- **Gets grow 44% from 1M to 100M**, mostly above 50M keys.
- **Cause: page-table walks.** Every random get misses the TLB (1.0 per op), and with 6 GB of data the page-table
  entries themselves no longer fit in cache. L1D and LLC misses per get rise by about 0.8 and 0.9.
- **The data access itself is still one line.** The extra misses are page-table loads, which huge pages remove (that
  is experiment 4's question).

## 3. The two-level rows layout up to 100M keys (experiment 2)

### get_hit: extra cost over flat

| keys | flat | rows-2M (table 16 KiB, L1) | rows-64K (512 KiB, L2) | rows-4K (8 MiB, L3) |
|---:|---:|---:|---:|---:|
| 1M | 13.0 | +3.8 | +3.9 | +5.5 |
| 10M | 13.6 | +5.4 | +7.1 | +7.8 |
| 25M | 13.9 | +5.7 | +6.7 | +8.8 |
| 50M | 14.5 | +5.5 | +7.2 | +9.8 |
| 75M | 16.9 | +5.2 | +7.2 | +12.1 (+0.49 LLC) |
| 100M | 18.7 | **+5.6** | +7.6 | **+14.0 (+0.72 LLC)** |

- **rows-2M: a constant +5.5 ns from 10M to 100M keys.** The table stays in L1 and never adds an LLC miss. At 100M
  keys this is +30% (24.3 vs 18.7 ns).
- **rows-64K: a constant +7.2 ns.** The table stays in L2.
- **rows-4K does not scale.** Its 8 MiB table is pushed out of the L3 by the data at 75M+ keys (+0.5 to +0.7 LLC
  misses) and needs its own TLB entries (1.5 dTLB misses per get at 100M). Its extra cost grows from 5.5 to 14 ns.
  **This layout should be dropped.**
- **Sequential gets:** +0.3 to +0.4 ns for every row size at every size.

### Inserts and misses

| keys | insert flat / rows-2M | get_miss flat / rows-2M / hash table |
|---:|---|---|
| 1M | 11.3 / 11.1 | 11.1 / **0.9** / 42.9 |
| 10M | 13.0 / 12.9 | 8.0 / **0.9** / 105.5 |
| 100M | 14.0 / 14.2 | 5.7 / **0.9** / 205.1 |

- **Inserts cost the same as flat at every size.**
- **A miss costs 0.9 ns at every size:** 4 cycles, 0 cache misses, answered from the table alone.
  - That is 6–12× faster than the flat array, which reads an untouched page and pays a TLB miss.
  - It is 47–228× faster than the hash table.

## 4. Key density (experiment 3)

| keys | pattern | flat | rows-4K | rows-64K | rows-2M |
|---:|---|---|---|---|---|
| 10M | dense | 64 B/key, 13.6 ns | 65 B, 21.1 | 64 B, 18.8 | 64 B, 18.6 |
| 10M | clustered | 67 B, 14.1 | 68 B, 21.9 | 124 B, 20.9 | **799 B**, 19.4 |
| 10M | sparse | **853 B**, 20.1 | 854 B, 34.0 | 859 B, 27.8 | 859 B, 25.6 |
| 100M | dense | 64 B, 18.7 | 64 B, 31.4 | 64 B, 25.8 | 64 B, 27.9 |
| 100M | clustered | 65 B, 19.3 | 65 B, 33.5 | 80 B, 26.9 | 86 B, 24.5 |
| 100M | sparse | 86 B, 20.1 | 86 B, 35.1 | 86 B, 28.1 | 86 B, 25.6 |

- **Memory waste is a low-density problem.**
  - At 10M keys in a 134M range, sparse keys cost 13× the minimum, and clustered keys cost 12× with 2 MiB rows.
  - At 100M keys (75% of the range used) every layout is within 1.0–1.35× of the 64 B minimum.
- **Speed hardly depends on the pattern** at 100M keys: flat 18.7–20.1 ns for all three.
- **For grouped keys, the row size must fit the group:** 64 KiB rows are the best trade-off for runs of 1,000 keys.

## 5. Baselines (experiment 5)

| keys | flat | rows-2M | hash table | skip list |
|---:|---:|---:|---:|---:|
| get_hit 1M | 13.0 | 16.8 | 31.0 | 742 |
| get_hit 10M | 13.6 | 19.1 | 45.2 | 1,497 |
| get_hit 100M | 18.7 | 24.3 | 32.6 | – |
| insert 100M | 14.0 | 14.2 | 79.2 | – |
| get_miss 100M | 5.7 | 0.9 | 205.1 | – |

- **Gets:** flat beats the hash table by 1.7–3.6×, and rows-2M beats it by 1.3–2.6×. The skip list is about 110×
  slower at 10M (it was not run above 10M).
- **Inserts at 100M:** flat and rows are 5.6× faster than the hash table.
- **The hash table's numbers jump around.**
  - Its gets are faster at 50M–100M (29–33 ns) than at 25M (50 ns): 3.4–3.7 vs 9.5 LLC misses per get.
  - Its capacity is a power of two, so its load factor changes in steps (0.56–0.75), and so do its probe lengths.
  - Treat the hash table as a range (29–50 ns per get), not a curve. Instructions per get stay around 106–114.

## 6. What this says

**Positive**
1. **O(1) holds to 100M keys:** identical instruction counts for the flat array and for rows-2M / 64K hits, inserts
   and dense misses.
2. **rows-2M costs a constant +5.5 ns on random gets** at any size: its table stays in L1, as designed.
3. **Inserts cost the same as flat; misses take 0.9 ns at any size.**
4. **Faster than the hash table everywhere** (1.3–2.6× on gets, 5.6× on inserts, about 230× on misses at 100M).

**Negative / limits**
1. **The time per get grows 44% from 1M to 100M keys** because of page-table walks. That's a property of base pages,
   to be checked against experiment 4 on huge pages.
2. **rows-4K does not scale** (its table falls out of the L3).
3. **Memory per key is poor for sparse or loosely clustered keys at low density.** It is fine at high density.

## 7. Next

Run the huge-page phase (`scripts/scale/run_scale.sh --phase huge`). It answers whether the growth of get time
above 50M keys is page-table walks (expected to disappear with 2 MiB pages, up to the roughly 4 GB the TLB can cover)
and how far beyond that it returns.
