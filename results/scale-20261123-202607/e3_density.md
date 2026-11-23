# DAT scale experiment 3: Key density: what on-demand rows save

How much memory does each layout need for dense, clustered and sparse keys, and what does it cost in speed?

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:28:21 |
| Kernel | Linux 6.8.0-138-generic |
| Machine | x86_64 |
| CPU | 12th Gen Intel(R) Core(TM) i5-12400F |
| Online CPUs | 12 |
| Base page size | 4096 B |
| Cache line size | 64 B |
| L1 Data cache | 48 KiB, 12-way, 64 sets, 64 B lines |
| L2 Unified cache | 1280 KiB, 10-way, 2048 sets, 64 B lines |
| L3 Unified cache | 18432 KiB, 9-way, 32768 sets, 64 B lines |
| Transparent huge pages | always [madvise] never (base mode opts out per mapping) |
| HugePages total / free | 0 / 0 (size 2048 kB) |
| perf_event_paranoid | 1 |
| Compiler | 11.4.0 |

## Configuration

| Setting | Value |
|---|---|
| Structures | flat, rows-4K, rows-64K, rows-2M |
| Pages | base |
| Key counts | 10M (key range 0 .. 2^27-1 = 134,217,728 keys) |
| Patterns | dense, clustered, sparse |
| Orders | rand |
| Operations | insert, get_hit, get_miss |
| Repetitions | 3 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.35 GiB |
| Peak process memory measured | 8.00 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory. They are computed in batches of 4096 outside the measured sections, so the times and counters below contain only the structure work.

## Memory plan

Computed before anything was allocated. Memory = structure + 256 MiB for the program.

| keys | pattern | structure | memory | decision |
|---|---|---|---:|---|
| 10M | dense | flat | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | dense | rows-4K | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | dense | rows-64K | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | dense | rows-2M | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | clustered | flat | 0.88 GiB | ✅ ran: fits: 0.88 GiB of 9.00 GiB |
| 10M | clustered | rows-4K | 0.88 GiB | ✅ ran: fits: 0.88 GiB of 9.00 GiB |
| 10M | clustered | rows-64K | 1.40 GiB | ✅ ran: fits: 1.40 GiB of 9.00 GiB |
| 10M | clustered | rows-2M | 7.69 GiB | ✅ ran: fits: 7.69 GiB of 9.00 GiB |
| 10M | sparse | flat | 8.20 GiB | ✅ ran: fits: 8.20 GiB of 9.00 GiB |
| 10M | sparse | rows-4K | 8.20 GiB | ✅ ran: fits: 8.20 GiB of 9.00 GiB |
| 10M | sparse | rows-64K | 8.25 GiB | ✅ ran: fits: 8.25 GiB of 9.00 GiB |
| 10M | sparse | rows-2M | 8.25 GiB | ✅ ran: fits: 8.25 GiB of 9.00 GiB |

## Checks

| Check | Target | Result | Worst | Where |
|---|---|---|---|---|
| Correctness (every get returned its own key) | all ok | ✅ PASS | - | - |
| O(1): instructions/op flat across key counts (flat) | max/min ≤ 1.10 | n/a | needs counters and ≥ 2 key counts | - |
| O(1): instructions/op flat across key counts (rows-4K) | max/min ≤ 1.10 | n/a | needs counters and ≥ 2 key counts | - |
| O(1): instructions/op flat across key counts (rows-64K) | max/min ≤ 1.10 | n/a | needs counters and ≥ 2 key counts | - |
| O(1): instructions/op flat across key counts (rows-2M) | max/min ≤ 1.10 | n/a | needs counters and ≥ 2 key counts | - |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 8.00 GiB | - |

## Key generation cost (NOT included in the measurements)

Keys are computed in batches of 4096 between measured sections; the timer and counters see only the structure work. For information, computing a key costs:

| pattern | rand ns/key |
|---|---:|
| dense | 9.6 |
| clustered | 23.9 |
| sparse | 10.2 |

## Memory per key

Bytes of memory holding data, per key (ran) or needed (skipped). A 64 B node is the minimum.

| keys | pattern | flat | rows-4K | rows-64K | rows-2M |
|---|---|---:|---:|---:|---:|
| 10M | dense | 64 | 65 | 64 | 64 |
| 10M | clustered | 67 | 68 | 124 | 799 |
| 10M | sparse | 853 | 854 | 859 | 859 |

## Results

Rows = key count, columns = structure. Medians per operation.

### dense / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 13.0 | 13.4 | 13.0 | 12.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 20.1 | 34.2 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.018 | 1.998 | 1.703 | 1.021 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.989 | 0.994 | 0.981 | 0.983 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.989 | 0.992 | 0.990 | 0.990 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 14.2 | 20.9 | 20.2 | 18.6 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 57.1 | 73.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.007 | 2.997 | 2.761 | 2.007 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.000 | 2.018 | 1.997 | 1.997 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.989 | 0.991 | 0.990 | 0.989 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 8.1 | 1.4 | 1.0 | 0.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 11.1 | 16.1 | 16.1 | 16.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.505 | 1.080 | 0.918 | 0.012 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.094 | 0.014 | 0.001 | 0.001 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.000 | 0.074 | 0.000 | 0.000 |

### clustered / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 13.2 | 13.7 | 13.3 | 13.4 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 20.1 | 34.2 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.607 | 2.398 | 2.047 | 2.086 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.996 | 1.003 | 0.998 | 1.008 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.988 | 1.552 | 0.992 | 0.990 |

### clustered / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 14.8 | 22.2 | 20.8 | 19.4 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 57.1 | 73.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.668 | 3.443 | 3.087 | 3.236 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.088 | 2.051 | 2.026 | 2.046 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.988 | 1.551 | 0.991 | 0.990 |

### clustered / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 5.3 | 1.5 | 3.4 | 9.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 11.1 | 16.1 | 16.7 | 24.4 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.492 | 1.055 | 1.061 | 2.868 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.012 | 0.012 | 0.077 | 0.948 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.989 | 0.113 | 0.067 | 0.919 |

### sparse / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 14.9 | 21.1 | 15.7 | 15.5 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 20.1 | 35.7 | 34.2 | 34.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.638 | 2.822 | 2.689 | 2.152 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.104 | 1.622 | 1.148 | 1.126 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.000 | 1.607 | 1.002 | 1.001 |

### sparse / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 20.8 | 33.9 | 28.1 | 29.3 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 57.1 | 73.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.701 | 3.905 | 3.748 | 3.304 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.824 | 3.548 | 2.866 | 2.760 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.000 | 1.605 | 1.001 | 1.000 |

### sparse / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 13.9 | 20.3 | 15.6 | 15.2 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 11.1 | 25.0 | 25.1 | 25.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.631 | 4.154 | 3.694 | 3.134 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.436 | 1.950 | 1.458 | 1.447 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.000 | 1.595 | 1.001 | 1.000 |

## Extra cost of each layout compared with the flat array

Cell = Δ ns/op (Δ LLC misses/op). This is the price of the row table.

**dense / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +0.4 (+0.005) | +0.0 (-0.008) | -0.0 (-0.006) |

**dense / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +6.7 (+0.019) | +6.0 (-0.003) | +4.3 (-0.003) |

**dense / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | -6.7 (-0.080) | -7.1 (-0.093) | -7.2 (-0.093) |

**clustered / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +0.6 (+0.006) | +0.2 (+0.002) | +0.3 (+0.012) |

**clustered / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +7.5 (-0.038) | +6.0 (-0.063) | +4.7 (-0.042) |

**clustered / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | -3.8 (-0.000) | -1.9 (+0.065) | +4.6 (+0.936) |

**sparse / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +6.1 (+0.517) | +0.8 (+0.044) | +0.6 (+0.022) |

**sparse / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +13.1 (+0.724) | +7.3 (+0.042) | +8.5 (-0.064) |

**sparse / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +6.4 (+0.514) | +1.7 (+0.022) | +1.3 (+0.011) |
