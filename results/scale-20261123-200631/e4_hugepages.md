# DAT scale experiment 4: Huge pages at scale

Do 2 MiB pages still remove TLB misses when the data is several GB? (Base-page numbers: experiment 2.)

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:10:45 |
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
| Repetitions | 2 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 12.78 GiB |
| Peak process memory measured | 0.60 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory; that computation is included in every number (see "Key generation cost").

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

## Key generation cost (included in every time below)

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.4 | 9.1 |

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
| 10M | 8.3 | 8.3 | 8.3 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 28.0 | 42.0 | 42.0 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.931 | 0.924 | 0.924 |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 4.7 | 4.9 | 4.9 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 58.0 | 72.0 | 72.0 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.028 | 0.011 | 0.010 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.914 | 0.892 | 0.892 |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 2.6 | 2.5 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 44.1 | 44.1 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.890 | 0.000 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.001 | 0.000 |

### dense / seq / update

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 8.5 | 8.6 | 8.6 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 31.0 | 45.0 | 45.0 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.000 | 0.000 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.913 | 0.892 | 0.888 |

### dense / rand / insert

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 13.6 | 14.0 | 13.7 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 60.3 | 74.3 | 74.3 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.484 | 0.001 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.982 | 0.983 | 0.983 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 21.6 | 26.1 | 25.2 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 90.3 | 103.9 | 103.9 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 1.000 | 1.621 | 1.011 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 1.957 | 1.958 | 1.963 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 11.2 | 11.7 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 76.4 | 76.4 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.902 | 0.000 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | - | 0.001 | 0.000 |

### dense / rand / update

Time per operation (ns/op)

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 14.1 | 14.4 | 14.3 |

Instructions per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 62.3 | 76.3 | 76.3 |

L1D load misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.000 | 0.485 | 0.001 |

LLC misses per operation

| keys | flat | rows-64K | rows-2M |
|---|---:|---:|---:|
| 10M | 0.984 | 0.983 | 0.982 |

## Extra cost of each layout compared with the flat array

Cell = Δ ns/op (Δ LLC misses/op). This is the price of the row table.

**dense / seq / insert**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.0 (-0.007) | +0.0 (-0.007) |

**dense / seq / get_hit**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.3 (-0.023) | +0.3 (-0.022) |

**dense / seq / get_miss**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | - | - |

**dense / seq / update**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.1 (-0.020) | +0.1 (-0.024) |

**dense / rand / insert**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.4 (+0.001) | +0.1 (+0.001) |

**dense / rand / get_hit**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +4.5 (+0.001) | +3.6 (+0.006) |

**dense / rand / get_miss**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | - | - |

**dense / rand / update**

| keys | rows-64K | rows-2M |
|---|---:|---:|
| 10M | +0.4 (-0.000) | +0.3 (-0.001) |
