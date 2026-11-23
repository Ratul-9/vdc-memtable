# Scale experiments: summary

Combined from the per-experiment CSV files. Times are ns per operation (medians), random order unless stated. Per-experiment reports (e1_*.md ... e5_*.md) have every table.

### Overview

| experiment | structures run | skipped (memory / time) | peak memory measured | wrong results |
|---|---|---|---|---|
| 1 | 3 | 0 | 1.49 GiB | ✅ 0 |
| 2 | 12 | 0 | 1.50 GiB | ✅ 0 |
| 3 | 12 | 0 | 8.00 GiB | ✅ 0 |
| 4 | 3 | 0 | 0.60 GiB | ✅ 0 |
| 5 | 5 | 1 | 2.00 GiB | ✅ 0 |

### 1. Flat array at scale (dense keys, random order, base pages)

| keys | insert | get_hit | get_miss | update | get_hit instructions |
|---|---|---|---|---|---|
| 1M | 12.2 | 19.4 | 15.0 | 13.3 | 80.8 |
| 10M | 14.3 | 23.9 | 15.4 | 14.7 | 90.3 |
| 25M | 14.1 | 22.1 | 12.8 | 14.5 | 85.3 |

### 2. Cost of the row table: get_hit, dense keys, rand order (cell = ns/op (Δ vs flat))

| keys | flat | rows-4K (table in L3) | rows-64K (table in L2) | rows-2M (table in L1) |
|---|---|---|---|---|
| 1M | 19.4 | 24.1 (+4.7) (+0.06 LLC) | 23.1 (+3.7) (+0.02 LLC) | 23.1 (+3.7) (+0.02 LLC) |
| 10M | 23.9 | 30.7 (+6.8) (+0.00 LLC) | 28.3 (+4.4) (+0.00 LLC) | 27.9 (+4.0) (+0.00 LLC) |
| 25M | 22.1 | 29.5 (+7.4) (+0.00 LLC) | 27.3 (+5.2) (+0.01 LLC) | 26.4 (+4.2) (+0.00 LLC) |

### 2. Cost of the row table: get_hit, dense keys, seq order (cell = ns/op (Δ vs flat))

| keys | flat | rows-4K (table in L3) | rows-64K (table in L2) | rows-2M (table in L1) |
|---|---|---|---|---|
| 1M | 4.8 | 5.1 (+0.2) (-0.04 LLC) | 4.8 (-0.0) (-0.13 LLC) | 4.8 (-0.0) (-0.13 LLC) |
| 10M | 4.7 | 5.0 (+0.3) (-0.02 LLC) | 5.0 (+0.3) (-0.02 LLC) | 5.0 (+0.3) (-0.03 LLC) |
| 25M | 4.8 | 5.0 (+0.2) (-0.01 LLC) | 5.0 (+0.2) (-0.02 LLC) | 5.0 (+0.2) (-0.02 LLC) |

### 3. Key density: memory per key and get_hit time (random order)

| keys | pattern | flat | rows-4K | rows-64K | rows-2M |
|---|---|---|---|---|---|
| 10M | dense | 64 B/key, 23.9 ns | 65 B/key, 30.5 ns | 64 B/key, 28.4 ns | 64 B/key, 27.9 ns |
| 10M | clustered | 67 B/key, 40.6 ns | 68 B/key, 49.1 ns | 124 B/key, 47.4 ns | 799 B/key, 45.4 ns |
| 10M | sparse | 853 B/key, 42.5 ns | 854 B/key, 60.0 ns | 859 B/key, 50.5 ns | 859 B/key, 50.2 ns |

### 4. Huge pages vs base pages: get_hit, dense keys, random order

| keys | structure | base ns | hugetlb ns | base dTLB/op | hugetlb dTLB/op |
|---|---|---|---|---|---|
| 10M | flat | 23.9 | 21.6 | 0.000 | 0.000 |
| 10M | rows-64K | 28.3 | 26.1 | 0.000 | 0.000 |
| 10M | rows-2M | 27.9 | 25.2 | 0.000 | 0.000 |

### 5. Against the baselines: get_hit, dense keys, random order (ns/op)

| keys | flat | rows-2M | hash table | skip list |
|---|---|---|---|---|
| 1M | 19.4 | 23.1 | 39.2 | 730.0 |
| 10M | 23.9 | 27.9 | 59.0 | 1543.8 |
| 25M | 22.1 | 26.4 | 59.9 | - |

### 5. Against the baselines: insert, dense keys, random order (ns/op)

| keys | flat | rows-2M | hash table | skip list |
|---|---|---|---|---|
| 1M | 12.2 | 11.8 | 37.4 | 575.9 |
| 10M | 14.3 | 14.9 | 54.7 | 1444.1 |
| 25M | 14.1 | 14.5 | 75.9 | - |

### 5. Against the baselines: get_miss, dense keys, random order (ns/op)

| keys | flat | rows-2M | hash table | skip list |
|---|---|---|---|---|
| 1M | 15.0 | 3.9 | 57.6 | 58.9 |
| 10M | 15.4 | 11.6 | 118.1 | 54.5 |
| 25M | 12.8 | 9.6 | 194.8 | - |

### O(1) check: worst max/min instructions per op across key counts (≤ 1.10)

| structure | pages | worst ratio | O(1) |
|---|---|---|---|
| flat | base | 1.19 | ❌ |
| hash | base | 1.74 | ❌ |
| rows-2M | base | 1.16 | ❌ |
| rows-4K | base | 1.16 | ❌ |
| rows-64K | base | 1.16 | ❌ |
| skiplist | base | 1.57 | ❌ |
