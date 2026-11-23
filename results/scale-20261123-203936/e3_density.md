# DAT scale experiment 3: Key density: what on-demand rows save

How much memory does each layout need for dense, clustered and sparse keys, and what does it cost in speed?

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:49:50 |
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
| Key counts | 10M, 100M (key range 0 .. 2^27-1 = 134,217,728 keys) |
| Patterns | dense, clustered, sparse |
| Orders | rand |
| Operations | insert, get_hit, get_miss |
| Repetitions | 3 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.40 GiB |
| Peak process memory measured | 8.01 GiB |
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
| 100M | dense | flat | 6.21 GiB | ✅ ran: fits: 6.21 GiB of 9.00 GiB |
| 100M | dense | rows-4K | 6.22 GiB | ✅ ran: fits: 6.22 GiB of 9.00 GiB |
| 100M | dense | rows-64K | 6.21 GiB | ✅ ran: fits: 6.21 GiB of 9.00 GiB |
| 100M | dense | rows-2M | 6.21 GiB | ✅ ran: fits: 6.21 GiB of 9.00 GiB |
| 100M | clustered | flat | 6.30 GiB | ✅ ran: fits: 6.30 GiB of 9.00 GiB |
| 100M | clustered | rows-4K | 6.30 GiB | ✅ ran: fits: 6.30 GiB of 9.00 GiB |
| 100M | clustered | rows-64K | 7.74 GiB | ✅ ran: fits: 7.74 GiB of 9.00 GiB |
| 100M | clustered | rows-2M | 8.25 GiB | ✅ ran: fits: 8.25 GiB of 9.00 GiB |
| 100M | sparse | flat | 8.25 GiB | ✅ ran: fits: 8.25 GiB of 9.00 GiB |
| 100M | sparse | rows-4K | 8.26 GiB | ✅ ran: fits: 8.26 GiB of 9.00 GiB |
| 100M | sparse | rows-64K | 8.25 GiB | ✅ ran: fits: 8.25 GiB of 9.00 GiB |
| 100M | sparse | rows-2M | 8.25 GiB | ✅ ran: fits: 8.25 GiB of 9.00 GiB |

## Checks

| Check | Target | Result | Worst | Where |
|---|---|---|---|---|
| Correctness (every get returned its own key) | all ok | ✅ PASS | - | - |
| O(1): instructions/op flat across key counts (flat) | max/min ≤ 1.10 | ✅ PASS | 1.00 | clustered / rand / get_miss |
| O(1): instructions/op flat across key counts (rows-4K) | max/min ≤ 1.10 | ✅ PASS | 1.04 | sparse / rand / insert |
| O(1): instructions/op flat across key counts (rows-64K) | max/min ≤ 1.10 | ❌ FAIL | 1.36 | clustered / rand / get_miss |
| O(1): instructions/op flat across key counts (rows-2M) | max/min ≤ 1.10 | ✅ PASS | 1.03 | clustered / rand / get_miss |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 8.01 GiB | - |

## Key generation cost (NOT included in the measurements)

Keys are computed in batches of 4096 between measured sections; the timer and counters see only the structure work. For information, computing a key costs:

| pattern | rand ns/key |
|---|---:|
| dense | 9.6 |
| clustered | 23.8 |
| sparse | 10.2 |

## Memory per key

Bytes of memory holding data, per key (ran) or needed (skipped). A 64 B node is the minimum.

| keys | pattern | flat | rows-4K | rows-64K | rows-2M |
|---|---|---:|---:|---:|---:|
| 10M | dense | 64 | 65 | 64 | 64 |
| 10M | clustered | 67 | 68 | 124 | 799 |
| 10M | sparse | 853 | 854 | 859 | 859 |
| 100M | dense | 64 | 64 | 64 | 64 |
| 100M | clustered | 65 | 65 | 80 | 86 |
| 100M | sparse | 86 | 86 | 86 | 86 |

## Results

Rows = key count, columns = structure. Medians per operation.

### dense / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 13.0 | 13.5 | 13.0 | 13.0 |
| 100M | 14.0 | 17.1 | 14.4 | 14.2 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 20.1 | 34.2 | 34.1 | 34.1 |
| 100M | 20.1 | 34.2 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.018 | 1.998 | 1.702 | 1.024 |
| 100M | 1.538 | 2.974 | 2.614 | 1.940 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.988 | 0.995 | 0.980 | 0.983 |
| 100M | 1.033 | 1.211 | 1.040 | 1.037 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.989 | 0.992 | 0.990 | 0.990 |
| 100M | 0.999 | 1.497 | 1.000 | 0.999 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 13.6 | 21.1 | 18.8 | 18.6 |
| 100M | 18.7 | 31.4 | 25.8 | 27.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 57.1 | 73.1 | 73.1 | 73.1 |
| 100M | 57.1 | 73.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.009 | 2.997 | 2.763 | 2.014 |
| 100M | 2.611 | 3.961 | 3.677 | 3.116 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.997 | 2.020 | 2.002 | 2.002 |
| 100M | 2.482 | 3.144 | 2.526 | 2.447 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.989 | 0.992 | 0.989 | 0.989 |
| 100M | 0.999 | 1.497 | 1.000 | 0.999 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 8.1 | 1.7 | 1.0 | 0.9 |
| 100M | 5.7 | 1.1 | 0.9 | 0.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 11.1 | 16.1 | 16.1 | 16.1 |
| 100M | 11.1 | 16.1 | 16.1 | 16.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.493 | 1.079 | 0.919 | 0.013 |
| 100M | 1.075 | 0.989 | 0.696 | 0.002 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.100 | 0.021 | 0.001 | 0.001 |
| 100M | 0.004 | 0.002 | 0.000 | 0.000 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.000 | 0.075 | 0.000 | 0.000 |
| 100M | 0.997 | 0.000 | 0.000 | 0.000 |

### clustered / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 13.2 | 13.7 | 13.4 | 13.4 |
| 100M | 14.2 | 18.3 | 14.8 | 14.4 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 20.1 | 34.2 | 34.1 | 34.1 |
| 100M | 20.1 | 34.2 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.607 | 2.396 | 2.047 | 2.088 |
| 100M | 1.638 | 3.076 | 2.672 | 2.154 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.997 | 1.004 | 0.999 | 1.009 |
| 100M | 1.051 | 1.321 | 1.070 | 1.051 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.988 | 1.551 | 0.992 | 0.991 |
| 100M | 1.000 | 1.603 | 1.000 | 1.000 |

### clustered / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 14.1 | 21.9 | 20.9 | 19.4 |
| 100M | 19.3 | 33.5 | 26.9 | 24.5 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 57.1 | 73.1 | 73.1 | 73.1 |
| 100M | 57.1 | 73.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.668 | 3.440 | 3.087 | 3.239 |
| 100M | 2.698 | 4.063 | 3.727 | 3.307 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.047 | 2.052 | 2.032 | 2.043 |
| 100M | 2.614 | 3.447 | 2.696 | 2.589 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.988 | 1.551 | 0.991 | 0.990 |
| 100M | 0.999 | 1.603 | 1.000 | 1.000 |

### clustered / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 5.3 | 1.5 | 3.4 | 9.9 |
| 100M | 6.5 | 3.6 | 9.3 | 10.8 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 11.1 | 16.1 | 16.7 | 24.4 |
| 100M | 11.1 | 16.4 | 22.8 | 25.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.487 | 1.055 | 1.061 | 2.873 |
| 100M | 1.559 | 1.252 | 2.910 | 3.128 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.014 | 0.013 | 0.077 | 0.948 |
| 100M | 0.050 | 0.050 | 0.765 | 1.028 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 0.989 | 0.113 | 0.067 | 0.918 |
| 100M | 0.997 | 0.244 | 0.746 | 0.998 |

### sparse / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 14.9 | 20.9 | 15.8 | 15.6 |
| 100M | 15.0 | 20.5 | 15.6 | 15.2 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 20.1 | 35.7 | 34.2 | 34.1 |
| 100M | 20.1 | 34.2 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.639 | 2.821 | 2.691 | 2.154 |
| 100M | 1.635 | 3.141 | 2.704 | 2.151 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.098 | 1.609 | 1.150 | 1.125 |
| 100M | 1.105 | 1.515 | 1.126 | 1.105 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.001 | 1.607 | 1.002 | 1.001 |
| 100M | 1.000 | 1.598 | 1.001 | 1.001 |

### sparse / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 20.1 | 34.0 | 27.8 | 25.6 |
| 100M | 20.1 | 35.1 | 28.1 | 25.6 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 57.1 | 73.1 | 73.1 | 73.1 |
| 100M | 57.1 | 73.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.705 | 3.903 | 3.749 | 3.308 |
| 100M | 2.699 | 4.079 | 3.755 | 3.303 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.790 | 3.542 | 2.843 | 2.751 |
| 100M | 2.792 | 3.699 | 2.846 | 2.757 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.000 | 1.606 | 1.001 | 1.000 |
| 100M | 1.000 | 1.599 | 1.001 | 1.001 |

### sparse / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 13.9 | 20.3 | 15.6 | 15.1 |
| 100M | 14.0 | 20.4 | 15.6 | 15.2 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 11.1 | 25.0 | 25.1 | 25.1 |
| 100M | 11.1 | 25.1 | 25.1 | 25.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 2.634 | 4.152 | 3.694 | 3.138 |
| 100M | 2.627 | 4.172 | 3.694 | 3.130 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.434 | 1.946 | 1.458 | 1.444 |
| 100M | 1.446 | 1.955 | 1.461 | 1.447 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 10M | 1.001 | 1.595 | 1.001 | 1.000 |
| 100M | 1.000 | 1.598 | 1.001 | 1.000 |

## Extra cost of each layout compared with the flat array

Cell = Δ ns/op (Δ LLC misses/op). This is the price of the row table.

**dense / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +0.4 (+0.007) | -0.0 (-0.008) | -0.1 (-0.005) |
| 100M | +3.1 (+0.178) | +0.4 (+0.007) | +0.2 (+0.004) |

**dense / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +7.4 (+0.023) | +5.2 (+0.005) | +4.9 (+0.005) |
| 100M | +12.8 (+0.662) | +7.1 (+0.044) | +9.2 (-0.035) |

**dense / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | -6.4 (-0.079) | -7.1 (-0.099) | -7.2 (-0.100) |
| 100M | -4.6 (-0.002) | -4.8 (-0.004) | -4.8 (-0.004) |

**clustered / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +0.6 (+0.008) | +0.2 (+0.002) | +0.3 (+0.012) |
| 100M | +4.1 (+0.270) | +0.6 (+0.019) | +0.2 (+0.001) |

**clustered / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +7.8 (+0.005) | +6.8 (-0.015) | +5.3 (-0.004) |
| 100M | +14.2 (+0.833) | +7.6 (+0.082) | +5.1 (-0.026) |

**clustered / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | -3.9 (-0.002) | -1.9 (+0.063) | +4.6 (+0.933) |
| 100M | -3.0 (+0.000) | +2.8 (+0.715) | +4.2 (+0.978) |

**sparse / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +6.1 (+0.511) | +0.9 (+0.052) | +0.7 (+0.027) |
| 100M | +5.5 (+0.410) | +0.7 (+0.021) | +0.2 (+0.001) |

**sparse / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +13.9 (+0.752) | +7.6 (+0.053) | +5.5 (-0.039) |
| 100M | +15.0 (+0.906) | +8.0 (+0.054) | +5.5 (-0.035) |

**sparse / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | +6.3 (+0.513) | +1.7 (+0.024) | +1.2 (+0.010) |
| 100M | +6.4 (+0.509) | +1.7 (+0.015) | +1.2 (+0.000) |
