# DAT scale experiment 4: Huge pages at scale

Do 2 MiB pages still remove TLB misses when the data is several GB? (Base-page numbers: experiment 2.)

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 21:03:36 |
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
| HugePages total / free | 3057 / 3057 (size 2048 kB) |
| perf_event_paranoid | 1 |
| Compiler | 11.4.0 |

## Configuration

| Setting | Value |
|---|---|
| Structures | flat, rows-64K, rows-2M |
| Pages | hugetlb |
| Key counts | 10M, 50M, 100M (key range 0 .. 2^27-1 = 134,217,728 keys) |
| Patterns | dense |
| Orders | seq, rand |
| Operations | insert, get_hit, get_miss, update |
| Repetitions | 3 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 7.36 GiB |
| Peak process memory measured | 5.96 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory. They are computed in batches of 4096 outside the measured sections, so the times and counters below contain only the structure work.

## Memory plan

Computed before anything was allocated. Memory = structure + 256 MiB for the program.

| keys | pattern | structure | memory | decision |
|---|---|---|---:|---|
| 10M | dense | flat | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | dense | rows-64K | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | dense | rows-2M | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 50M | dense | flat | 3.23 GiB | ✅ ran: fits: 3.23 GiB of 9.00 GiB |
| 50M | dense | rows-64K | 3.23 GiB | ✅ ran: fits: 3.23 GiB of 9.00 GiB |
| 50M | dense | rows-2M | 3.23 GiB | ✅ ran: fits: 3.23 GiB of 9.00 GiB |
| 100M | dense | flat | 6.21 GiB | ✅ ran: fits: 6.21 GiB of 9.00 GiB |
| 100M | dense | rows-64K | 6.21 GiB | ✅ ran: fits: 6.21 GiB of 9.00 GiB |
| 100M | dense | rows-2M | 6.21 GiB | ✅ ran: fits: 6.21 GiB of 9.00 GiB |

## Checks

| Check | Target | Result | Worst | Where |
|---|---|---|---|---|
| Correctness (every get returned its own key) | all ok | ✅ PASS | - | - |
| O(1): instructions/op flat across key counts (flat) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / seq / insert |
| O(1): instructions/op flat across key counts (rows-64K) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / rand / get_miss |
| O(1): instructions/op flat across key counts (rows-2M) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / rand / get_miss |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 5.96 GiB | - |

## Key generation cost (NOT included in the measurements)

Keys are computed in batches of 4096 between measured sections; the timer and counters see only the structure work. For information, computing a key costs:

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.4 | 9.6 |

## Memory per key

Bytes of memory holding data, per key (ran) or needed (skipped). A 64 B node is the minimum.

| keys | pattern | flat | rows-64K | rows-2M |
|---|---|---:|---:|---:|
| 10M | dense | 64 | 64 | 64 |
| 50M | dense | 64 | 64 | 64 |
| 100M | dense | 64 | 64 | 64 |

## Results

Rows = key count, columns = structure. Medians per operation.

### dense / seq / insert

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 7.9 | 7.9 | 7.9 |
| 50M | 7.8 | 7.9 | 7.9 |
| 100M | 7.8 | 7.8 | 7.8 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 20.1 | 34.1 | 34.1 |
| 50M | 20.1 | 34.1 | 34.1 |
| 100M | 20.1 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.012 | 0.015 | 0.015 |
| 50M | 0.011 | 0.015 | 0.015 |
| 100M | 0.011 | 0.015 | 0.015 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.930 | 0.926 | 0.926 |
| 50M | 0.930 | 0.931 | 0.931 |
| 100M | 0.931 | 0.932 | 0.932 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |
| 50M | 0.000 | 0.000 | 0.000 |
| 100M | 0.000 | 0.000 | 0.000 |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 4.4 | 4.8 | 4.8 |
| 50M | 4.4 | 4.8 | 4.8 |
| 100M | 4.4 | 4.8 | 4.8 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 57.1 | 73.1 | 73.1 |
| 50M | 57.1 | 73.1 | 73.1 |
| 100M | 57.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.212 | 0.151 | 0.150 |
| 50M | 0.210 | 0.151 | 0.149 |
| 100M | 0.210 | 0.151 | 0.151 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.922 | 0.873 | 0.873 |
| 50M | 0.923 | 0.882 | 0.884 |
| 100M | 0.924 | 0.884 | 0.884 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |
| 50M | 0.000 | 0.000 | 0.000 |
| 100M | 0.000 | 0.000 | 0.000 |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 1.0 | 0.9 |
| 50M | - | 0.9 | 0.8 |
| 100M | - | 0.9 | 0.9 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 16.1 | 16.1 |
| 50M | - | 16.1 | 16.1 |
| 100M | - | 16.1 | 16.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.898 | 0.011 |
| 50M | - | 0.863 | 0.005 |
| 100M | - | 0.684 | 0.002 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.001 | 0.000 |
| 50M | - | 0.000 | 0.000 |
| 100M | - | 0.000 | 0.000 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.000 | 0.000 |
| 50M | - | 0.000 | 0.000 |
| 100M | - | 0.000 | 0.000 |

### dense / seq / update

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 8.2 | 8.3 | 8.3 |
| 50M | 8.1 | 8.3 | 8.3 |
| 100M | 8.1 | 8.3 | 8.3 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 25.1 | 40.1 | 40.1 |
| 50M | 25.1 | 40.1 | 40.1 |
| 100M | 25.1 | 40.1 | 40.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.005 | 0.004 | 0.005 |
| 50M | 0.006 | 0.005 | 0.005 |
| 100M | 0.005 | 0.004 | 0.005 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.913 | 0.891 | 0.891 |
| 50M | 0.914 | 0.901 | 0.900 |
| 100M | 0.917 | 0.902 | 0.902 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |
| 50M | 0.000 | 0.000 | 0.000 |
| 100M | 0.000 | 0.000 | 0.000 |

### dense / rand / insert

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 12.7 | 12.7 | 12.7 |
| 50M | 12.9 | 12.9 | 12.9 |
| 100M | 12.9 | 13.0 | 12.9 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 20.1 | 34.1 | 34.1 |
| 50M | 20.1 | 34.1 | 34.1 |
| 100M | 20.1 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.028 | 0.555 | 0.030 |
| 50M | 0.170 | 1.175 | 0.234 |
| 100M | 0.357 | 1.445 | 0.617 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.984 | 0.984 | 0.984 |
| 50M | 0.997 | 0.998 | 0.997 |
| 100M | 0.999 | 1.000 | 0.999 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |
| 50M | 0.464 | 0.464 | 0.467 |
| 100M | 0.736 | 0.733 | 0.739 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 12.3 | 18.0 | 16.9 |
| 50M | 12.4 | 18.2 | 17.2 |
| 100M | 12.6 | 18.9 | 17.6 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 57.1 | 73.1 | 73.1 |
| 50M | 57.1 | 73.1 | 73.1 |
| 100M | 57.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 1.012 | 1.632 | 1.006 |
| 50M | 1.234 | 2.219 | 1.334 |
| 100M | 1.452 | 2.491 | 1.815 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 1.967 | 1.973 | 1.991 |
| 50M | 1.987 | 1.991 | 1.986 |
| 100M | 1.993 | 2.015 | 1.994 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |
| 50M | 0.464 | 0.462 | 0.467 |
| 100M | 0.736 | 0.728 | 0.738 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 1.0 | 0.9 |
| 50M | - | 1.0 | 0.8 |
| 100M | - | 0.9 | 0.8 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 16.1 | 16.1 |
| 50M | - | 16.1 | 16.1 |
| 100M | - | 16.1 | 16.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.918 | 0.013 |
| 50M | - | 0.877 | 0.006 |
| 100M | - | 0.697 | 0.002 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.001 | 0.001 |
| 50M | - | 0.000 | 0.000 |
| 100M | - | 0.000 | 0.000 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.000 | 0.000 |
| 50M | - | 0.000 | 0.000 |
| 100M | - | 0.000 | 0.000 |

### dense / rand / update

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 12.9 | 13.0 | 13.0 |
| 50M | 13.1 | 13.2 | 13.2 |
| 100M | 13.2 | 13.2 | 13.3 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 25.1 | 40.1 | 40.1 |
| 50M | 25.1 | 40.1 | 40.1 |
| 100M | 25.1 | 40.1 | 40.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.022 | 0.545 | 0.018 |
| 50M | 0.165 | 1.163 | 0.224 |
| 100M | 0.353 | 1.433 | 0.609 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.985 | 0.991 | 0.986 |
| 50M | 0.996 | 0.997 | 0.996 |
| 100M | 0.999 | 0.999 | 1.000 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |
| 50M | 0.464 | 0.463 | 0.467 |
| 100M | 0.736 | 0.729 | 0.738 |

## Extra cost of each layout compared with the flat array

Cell = Δ ns/op (Δ LLC misses/op). This is the price of the row table.

**dense / seq / insert**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | -0.0 (-0.003) | +0.0 (-0.003) |
| 50M | +0.0 (+0.001) | +0.0 (+0.001) |
| 100M | +0.0 (+0.001) | +0.0 (+0.001) |

**dense / seq / get_hit**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.4 (-0.049) | +0.4 (-0.048) |
| 50M | +0.4 (-0.041) | +0.4 (-0.039) |
| 100M | +0.4 (-0.040) | +0.4 (-0.040) |

**dense / seq / get_miss**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | - | - |
| 50M | - | - |
| 100M | - | - |

**dense / seq / update**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.1 (-0.023) | +0.1 (-0.022) |
| 50M | +0.1 (-0.013) | +0.2 (-0.015) |
| 100M | +0.2 (-0.015) | +0.2 (-0.015) |

**dense / rand / insert**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.0 (+0.000) | +0.0 (+0.000) |
| 50M | +0.0 (+0.001) | +0.0 (-0.000) |
| 100M | +0.1 (+0.001) | +0.0 (+0.000) |

**dense / rand / get_hit**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +5.7 (+0.006) | +4.6 (+0.024) |
| 50M | +5.8 (+0.004) | +4.8 (-0.001) |
| 100M | +6.2 (+0.022) | +5.0 (+0.000) |

**dense / rand / get_miss**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | - | - |
| 50M | - | - |
| 100M | - | - |

**dense / rand / update**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.0 (+0.006) | +0.1 (+0.001) |
| 50M | +0.0 (+0.000) | +0.1 (+0.000) |
| 100M | +0.1 (+0.000) | +0.1 (+0.001) |
