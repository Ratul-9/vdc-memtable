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
| 1M | 11.1 | 12.9 | 11.4 | 12.5 | 57.1 |
| 10M | 13.0 | 13.7 | 8.1 | 13.3 | 57.1 |
| 25M | 13.2 | 13.9 | 7.4 | 13.5 | 57.1 |

### 2. Cost of the row table: get_hit, dense keys, rand order (cell = ns/op (Δ vs flat))

| keys | flat | rows-4K (table in L3) | rows-64K (table in L2) | rows-2M (table in L1) |
|---|---|---|---|---|
| 1M | 12.9 | 17.6 (+4.6) (+0.05 LLC) | 16.4 (+3.5) (+0.02 LLC) | 16.4 (+3.5) (+0.02 LLC) |
| 10M | 13.7 | 21.0 (+7.3) (+0.01 LLC) | 20.2 (+6.5) (-0.01 LLC) | 18.5 (+4.8) (-0.02 LLC) |
| 25M | 13.9 | 22.0 (+8.1) (+0.03 LLC) | 20.6 (+6.7) (+0.02 LLC) | 19.0 (+5.1) (+0.00 LLC) |

### 2. Cost of the row table: get_hit, dense keys, seq order (cell = ns/op (Δ vs flat))

| keys | flat | rows-4K (table in L3) | rows-64K (table in L2) | rows-2M (table in L1) |
|---|---|---|---|---|
| 1M | 4.5 | 4.9 (+0.4) (-0.05 LLC) | 4.8 (+0.3) (-0.11 LLC) | 4.8 (+0.3) (-0.13 LLC) |
| 10M | 4.4 | 4.8 (+0.4) (-0.03 LLC) | 4.8 (+0.4) (-0.03 LLC) | 4.8 (+0.4) (-0.03 LLC) |
| 25M | 4.4 | 4.8 (+0.3) (-0.03 LLC) | 4.8 (+0.4) (-0.03 LLC) | 4.8 (+0.4) (-0.03 LLC) |

### 3. Key density: memory per key and get_hit time (random order)

| keys | pattern | flat | rows-4K | rows-64K | rows-2M |
|---|---|---|---|---|---|
| 10M | dense | 64 B/key, 14.2 ns | 65 B/key, 20.9 ns | 64 B/key, 20.2 ns | 64 B/key, 18.6 ns |
| 10M | clustered | 67 B/key, 14.8 ns | 68 B/key, 22.2 ns | 124 B/key, 20.8 ns | 799 B/key, 19.4 ns |
| 10M | sparse | 853 B/key, 20.8 ns | 854 B/key, 33.9 ns | 859 B/key, 28.1 ns | 859 B/key, 29.3 ns |

### 4. Huge pages vs base pages: get_hit, dense keys, random order

| keys | structure | base ns | hugetlb ns | base dTLB/op | hugetlb dTLB/op |
|---|---|---|---|---|---|
| 10M | flat | 13.7 | 12.3 | 0.990 | 0.000 |
| 10M | rows-64K | 20.2 | 16.4 | 0.990 | 0.000 |
| 10M | rows-2M | 18.5 | 16.2 | 0.989 | 0.000 |

### 5. Against the baselines: get_hit, dense keys, random order (ns/op)

| keys | flat | rows-2M | hash table | skip list |
|---|---|---|---|---|
| 1M | 12.9 | 16.4 | 30.9 | 730.4 |
| 10M | 13.7 | 18.5 | 45.2 | 1499.7 |
| 25M | 13.9 | 19.0 | 50.2 | - |

### 5. Against the baselines: insert, dense keys, random order (ns/op)

| keys | flat | rows-2M | hash table | skip list |
|---|---|---|---|---|
| 1M | 11.1 | 11.0 | 31.3 | 545.2 |
| 10M | 13.0 | 13.0 | 42.3 | 1303.3 |
| 25M | 13.2 | 13.2 | 66.7 | - |

### 5. Against the baselines: get_miss, dense keys, random order (ns/op)

| keys | flat | rows-2M | hash table | skip list |
|---|---|---|---|---|
| 1M | 11.4 | 0.9 | 42.5 | 53.7 |
| 10M | 8.1 | 0.9 | 105.2 | 46.1 |
| 25M | 7.4 | 0.8 | 181.8 | - |

### O(1) check: worst max/min instructions per op across key counts (≤ 1.10)

| structure | pages | worst ratio | O(1) |
|---|---|---|---|
| flat | base | 1.00 | ✅ |
| hash | base | 2.02 | ❌ |
| rows-2M | base | 1.00 | ✅ |
| rows-4K | base | 1.00 | ✅ |
| rows-64K | base | 1.00 | ✅ |
| skiplist | base | 1.66 | ❌ |
