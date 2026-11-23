# DAT scale experiment 3: Key density: what on-demand rows save

How much memory does each layout need for dense, clustered and sparse keys, and what does it cost in speed?

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:08:06 |
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
| Repetitions | 2 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.29 GiB |
| Peak process memory measured | 8.00 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory; that computation is included in every number (see "Key generation cost").

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

## Key generation cost (included in every time below)

| pattern | rand ns/key |
|---|---:|
| dense | 9.1 |
| clustered | 23.6 |
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
| 10M | 14.5 | 16.2 | 15.1 | 14.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 60.3 | 74.4 | 74.3 | 74.3 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.973 | 1.947 | 1.638 | 0.977 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.988 | 0.995 | 0.980 | 0.982 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 23.9 | 30.5 | 28.4 | 27.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 90.3 | 103.9 | 103.9 | 103.9 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.986 | 2.968 | 2.721 | 2.007 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.962 | 1.964 | 1.964 | 1.960 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 15.6 | 12.1 | 11.7 | 11.7 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 72.5 | 76.4 | 76.4 | 76.4 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.413 | 1.041 | 0.902 | 0.000 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.111 | 0.015 | 0.001 | 0.000 |

### clustered / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 26.6 | 29.1 | 27.6 | 27.1 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 101.4 | 115.5 | 115.4 | 115.4 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.552 | 2.337 | 1.989 | 2.015 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.992 | 0.999 | 0.997 | 1.004 |

### clustered / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 40.6 | 49.1 | 47.4 | 45.4 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 131.4 | 144.5 | 144.5 | 144.5 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.647 | 3.394 | 3.047 | 3.186 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.986 | 1.972 | 1.981 | 2.001 |

### clustered / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 24.5 | 24.6 | 27.0 | 37.7 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 91.8 | 95.8 | 96.5 | 104.2 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.413 | 1.032 | 1.039 | 2.820 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.012 | 0.013 | 0.077 | 0.946 |

### sparse / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 20.0 | 39.1 | 25.1 | 22.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 84.3 | 100.0 | 98.4 | 98.3 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.589 | 2.780 | 2.646 | 2.090 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.106 | 1.619 | 1.163 | 1.136 |

### sparse / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 42.5 | 60.0 | 50.5 | 50.2 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 114.3 | 127.9 | 127.9 | 127.9 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.682 | 3.861 | 3.712 | 3.260 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.690 | 3.449 | 2.789 | 2.707 |

### sparse / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 35.3 | 53.7 | 42.6 | 40.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 71.9 | 84.8 | 84.9 | 84.9 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.591 | 4.110 | 3.657 | 3.095 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.438 | 1.955 | 1.465 | 1.451 |

## Extra cost of each layout compared with the flat array

Cell = Δ ns/op (Δ LLC misses/op). This is the price of the row table.

**dense / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +1.7 (+0.006) | +0.6 (-0.009) | +0.4 (-0.006) |

**dense / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +6.6 (+0.002) | +4.5 (+0.002) | +4.0 (-0.002) |

**dense / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | -3.5 (-0.096) | -3.9 (-0.110) | -3.9 (-0.110) |

**clustered / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +2.5 (+0.007) | +1.1 (+0.005) | +0.5 (+0.012) |

**clustered / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +8.6 (-0.014) | +6.8 (-0.005) | +4.8 (+0.015) |

**clustered / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +0.1 (+0.001) | +2.5 (+0.066) | +13.2 (+0.935) |

**sparse / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +19.1 (+0.514) | +5.1 (+0.058) | +2.8 (+0.030) |

**sparse / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +17.4 (+0.759) | +8.0 (+0.099) | +7.7 (+0.017) |

**sparse / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +18.4 (+0.517) | +7.2 (+0.027) | +5.6 (+0.013) |
