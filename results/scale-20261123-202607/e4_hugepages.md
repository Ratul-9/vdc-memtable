# DAT scale experiment 4: Huge pages at scale

Do 2 MiB pages still remove TLB misses when the data is several GB? (Base-page numbers: experiment 2.)

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:35:26 |
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
| HugePages total / free | 311 / 311 (size 2048 kB) |
| perf_event_paranoid | 1 |
| Compiler | 11.4.0 |

## Configuration

| Setting | Value |
|---|---|
| Structures | flat, rows-64K, rows-2M |
| Pages | hugetlb |
| Key counts | 10M (key range 0 .. 2^27-1 = 134,217,728 keys) |
| Patterns | dense |
| Orders | seq, rand |
| Operations | insert, get_hit, get_miss, update |
| Repetitions | 3 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 12.77 GiB |
| Peak process memory measured | 0.60 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory. They are computed in batches of 4096 outside the measured sections, so the times and counters below contain only the structure work.

## Memory plan

Computed before anything was allocated. Memory = structure + 256 MiB for the program.

| keys | pattern | structure | memory | decision |
|---|---|---|---:|---|
| 10M | dense | flat | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | dense | rows-64K | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | dense | rows-2M | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |

## Checks

| Check | Target | Result | Worst | Where |
|---|---|---|---|---|
| Correctness (every get returned its own key) | all ok | ✅ PASS | - | - |
| O(1): instructions/op flat across key counts (flat) | max/min ≤ 1.10 | n/a | needs counters and ≥ 2 key counts | - |
| O(1): instructions/op flat across key counts (rows-64K) | max/min ≤ 1.10 | n/a | needs counters and ≥ 2 key counts | - |
| O(1): instructions/op flat across key counts (rows-2M) | max/min ≤ 1.10 | n/a | needs counters and ≥ 2 key counts | - |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 0.60 GiB | - |

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

## Results

Rows = key count, columns = structure. Medians per operation.

### dense / seq / insert

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 7.9 | 7.9 | 7.9 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 20.1 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.011 | 0.015 | 0.015 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.930 | 0.927 | 0.926 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 4.4 | 4.7 | 4.7 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 57.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.066 | 0.019 | 0.018 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.921 | 0.887 | 0.885 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 1.0 | 0.9 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 16.1 | 16.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.897 | 0.010 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.001 | 0.000 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.000 | 0.000 |

### dense / seq / update

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 8.2 | 8.3 | 8.3 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 25.1 | 40.1 | 40.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.005 | 0.004 | 0.004 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.913 | 0.893 | 0.891 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |

### dense / rand / insert

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 12.7 | 12.7 | 12.7 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 20.1 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.029 | 0.555 | 0.030 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.986 | 0.988 | 0.985 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 12.3 | 16.4 | 16.3 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 57.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 1.011 | 1.630 | 1.005 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 1.967 | 1.971 | 1.988 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 1.0 | 0.9 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 16.1 | 16.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.917 | 0.011 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.001 | 0.001 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.000 | 0.000 |

### dense / rand / update

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 12.9 | 12.9 | 13.0 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 25.1 | 40.1 | 40.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.022 | 0.544 | 0.018 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.982 | 0.985 | 0.990 |

dTLB misses per operation (load + store)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |

## Extra cost of each layout compared with the flat array

Cell = Δ ns/op (Δ LLC misses/op). This is the price of the row table.

**dense / seq / insert**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | -0.0 (-0.003) | -0.0 (-0.004) |

**dense / seq / get_hit**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.3 (-0.035) | +0.3 (-0.037) |

**dense / seq / get_miss**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | - | - |

**dense / seq / update**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.1 (-0.021) | +0.1 (-0.022) |

**dense / rand / insert**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | -0.0 (+0.002) | +0.0 (-0.001) |

**dense / rand / get_hit**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +4.2 (+0.004) | +4.0 (+0.021) |

**dense / rand / get_miss**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | - | - |

**dense / rand / update**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.0 (+0.002) | +0.1 (+0.007) |
