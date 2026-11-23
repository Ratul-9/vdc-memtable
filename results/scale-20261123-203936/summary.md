# Scale experiments: summary

Combined from the per-experiment CSV files. Times are ns per operation (medians), random order unless stated. Per-experiment reports (e1_*.md ... e5_*.md) have every table.

### Overview

| experiment | structures run | skipped (memory / time) | peak memory measured | wrong results |
|---|---|---|---|---|
| 1 | 6 | 0 | 5.96 GiB | ✅ 0 |
| 2 | 24 | 0 | 5.97 GiB | ✅ 0 |
| 3 | 24 | 0 | 8.01 GiB | ✅ 0 |
| 5 | 8 | 4 | 8.00 GiB | ✅ 0 |

### 1. Flat array at scale (dense keys, random order, base pages)

| keys | insert | get_hit | get_miss | update | get_hit instructions |
|---|---|---|---|---|---|
| 1M | 11.3 | 13.0 | 11.1 | 12.6 | 57.1 |
| 10M | 13.0 | 13.6 | 8.0 | 13.4 | 57.1 |
| 25M | 13.3 | 13.9 | 7.3 | 13.5 | 57.1 |
| 50M | 13.4 | 14.5 | 6.7 | 13.6 | 57.1 |
| 75M | 13.7 | 16.9 | 5.9 | 13.8 | 57.1 |
| 100M | 14.0 | 18.7 | 5.7 | 14.2 | 57.1 |

### 2. Cost of the row table: get_hit, dense keys, rand order (cell = ns/op (Δ vs flat))

| keys | flat | rows-4K (table in L3) | rows-64K (table in L2) | rows-2M (table in L1) |
|---|---|---|---|---|
| 1M | 13.0 | 18.4 (+5.5) (+0.04 LLC) | 16.9 (+3.9) (+0.00 LLC) | 16.8 (+3.8) (-0.01 LLC) |
| 10M | 13.6 | 21.4 (+7.8) (+0.03 LLC) | 20.7 (+7.1) (+0.01 LLC) | 19.1 (+5.4) (+0.00 LLC) |
| 25M | 13.9 | 22.7 (+8.8) (+0.01 LLC) | 20.6 (+6.7) (+0.00 LLC) | 19.6 (+5.7) (-0.01 LLC) |
| 50M | 14.5 | 24.4 (+9.8) (+0.12 LLC) | 21.8 (+7.2) (+0.02 LLC) | 20.1 (+5.5) (-0.00 LLC) |
| 75M | 16.9 | 29.0 (+12.1) (+0.49 LLC) | 24.1 (+7.2) (+0.02 LLC) | 22.1 (+5.2) (-0.03 LLC) |
| 100M | 18.7 | 32.7 (+14.0) (+0.72 LLC) | 26.3 (+7.6) (+0.03 LLC) | 24.3 (+5.6) (-0.03 LLC) |

### 2. Cost of the row table: get_hit, dense keys, seq order (cell = ns/op (Δ vs flat))

| keys | flat | rows-4K (table in L3) | rows-64K (table in L2) | rows-2M (table in L1) |
|---|---|---|---|---|
| 1M | 4.6 | 5.0 (+0.4) (-0.05 LLC) | 4.8 (+0.2) (-0.14 LLC) | 4.8 (+0.2) (-0.13 LLC) |
| 10M | 4.5 | 4.8 (+0.3) (-0.04 LLC) | 4.9 (+0.4) (-0.04 LLC) | 4.8 (+0.3) (-0.04 LLC) |
| 25M | 4.5 | 4.8 (+0.3) (-0.04 LLC) | 4.8 (+0.4) (-0.03 LLC) | 4.8 (+0.3) (-0.03 LLC) |
| 50M | 4.5 | 4.8 (+0.4) (-0.04 LLC) | 4.8 (+0.4) (-0.03 LLC) | 4.8 (+0.4) (-0.02 LLC) |
| 75M | 4.5 | 4.8 (+0.4) (-0.03 LLC) | 4.8 (+0.4) (-0.03 LLC) | 4.8 (+0.3) (-0.02 LLC) |
| 100M | 4.5 | 4.8 (+0.4) (-0.04 LLC) | 4.8 (+0.4) (-0.03 LLC) | 4.8 (+0.4) (-0.03 LLC) |

### 3. Key density: memory per key and get_hit time (random order)

| keys | pattern | flat | rows-4K | rows-64K | rows-2M |
|---|---|---|---|---|---|
| 10M | dense | 64 B/key, 13.6 ns | 65 B/key, 21.1 ns | 64 B/key, 18.8 ns | 64 B/key, 18.6 ns |
| 10M | clustered | 67 B/key, 14.1 ns | 68 B/key, 21.9 ns | 124 B/key, 20.9 ns | 799 B/key, 19.4 ns |
| 10M | sparse | 853 B/key, 20.1 ns | 854 B/key, 34.0 ns | 859 B/key, 27.8 ns | 859 B/key, 25.6 ns |
| 100M | dense | 64 B/key, 18.7 ns | 64 B/key, 31.4 ns | 64 B/key, 25.8 ns | 64 B/key, 27.9 ns |
| 100M | clustered | 65 B/key, 19.3 ns | 65 B/key, 33.5 ns | 80 B/key, 26.9 ns | 86 B/key, 24.5 ns |
| 100M | sparse | 86 B/key, 20.1 ns | 86 B/key, 35.1 ns | 86 B/key, 28.1 ns | 86 B/key, 25.6 ns |

### 5. Against the baselines: get_hit, dense keys, random order (ns/op)

| keys | flat | rows-2M | hash table | skip list |
|---|---|---|---|---|
| 1M | 13.0 | 16.8 | 31.0 | 741.7 |
| 10M | 13.6 | 19.1 | 45.2 | 1496.8 |
| 25M | 13.9 | 19.6 | 50.3 | - |
| 50M | 14.5 | 20.1 | 29.4 | - |
| 75M | 16.9 | 22.1 | 32.6 | - |
| 100M | 18.7 | 24.3 | 32.6 | - |

### 5. Against the baselines: insert, dense keys, random order (ns/op)

| keys | flat | rows-2M | hash table | skip list |
|---|---|---|---|---|
| 1M | 11.3 | 11.1 | 31.3 | 544.0 |
| 10M | 13.0 | 12.9 | 42.6 | 1303.0 |
| 25M | 13.3 | 13.3 | 67.3 | - |
| 50M | 13.4 | 13.5 | 72.9 | - |
| 75M | 13.7 | 13.7 | 47.7 | - |
| 100M | 14.0 | 14.2 | 79.2 | - |

### 5. Against the baselines: get_miss, dense keys, random order (ns/op)

| keys | flat | rows-2M | hash table | skip list |
|---|---|---|---|---|
| 1M | 11.1 | 0.9 | 42.9 | 53.3 |
| 10M | 8.0 | 0.9 | 105.5 | 44.8 |
| 25M | 7.3 | 0.8 | 181.6 | - |
| 50M | 6.7 | 0.8 | 195.6 | - |
| 75M | 5.9 | 0.8 | 107.3 | - |
| 100M | 5.7 | 0.9 | 205.1 | - |

### O(1) check: worst max/min instructions per op across key counts (≤ 1.10)

| structure | pages | worst ratio | O(1) |
|---|---|---|---|
| flat | base | 1.00 | ✅ |
| hash | base | 2.02 | ❌ |
| rows-2M | base | 1.03 | ✅ |
| rows-4K | base | 1.04 | ✅ |
| rows-64K | base | 1.36 | ❌ |
| skiplist | base | 1.66 | ❌ |
