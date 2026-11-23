# DAT scale experiment 1: Flat array at scale (reference)

How does the flat array (slot = key) behave from 1M to 100M keys? Every later result is compared with this.

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:26:25 |
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
| Structures | flat |
| Pages | base |
| Key counts | 1M, 10M, 25M (key range 0 .. 2^27-1 = 134,217,728 keys) |
| Patterns | dense |
| Orders | seq, rand |
| Operations | insert, get_hit, get_miss, update |
| Repetitions | 3 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.28 GiB |
| Peak process memory measured | 1.49 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory. They are computed in batches of 4096 outside the measured sections, so the times and counters below contain only the structure work.

## Memory plan

Computed before anything was allocated. Memory = structure + 256 MiB for the program.

| keys | pattern | structure | memory | decision |
|---|---|---|---:|---|
| 1M | dense | flat | 0.31 GiB | ✅ ran: fits: 0.31 GiB of 9.00 GiB |
| 10M | dense | flat | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 25M | dense | flat | 1.74 GiB | ✅ ran: fits: 1.74 GiB of 9.00 GiB |

## Checks

| Check | Target | Result | Worst | Where |
|---|---|---|---|---|
| Correctness (every get returned its own key) | all ok | ✅ PASS | - | - |
| O(1): instructions/op flat across key counts (flat) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / rand / get_miss |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 1.49 GiB | - |

## Key generation cost (NOT included in the measurements)

Keys are computed in batches of 4096 between measured sections; the timer and counters see only the structure work. For information, computing a key costs:

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.4 | 9.6 |

## Memory per key

Bytes of memory holding data, per key (ran) or needed (skipped). A 64 B node is the minimum.

| keys | pattern | flat |
|---|---|---:|
| 1M | dense | 64 |
| 10M | dense | 64 |
| 25M | dense | 64 |

## Results

Rows = key count, columns = structure. Medians per operation.

### dense / seq / insert

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 7.5 |
| 10M | 7.9 |
| 25M | 7.9 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 20.1 |
| 10M | 20.1 |
| 25M | 20.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.011 |
| 10M | 0.011 |
| 25M | 0.011 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.888 |
| 10M | 0.939 |
| 25M | 0.939 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.015 |
| 10M | 0.016 |
| 25M | 0.016 |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 4.5 |
| 10M | 4.4 |
| 25M | 4.4 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 57.1 |
| 10M | 57.1 |
| 25M | 57.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.078 |
| 10M | 0.067 |
| 25M | 0.067 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.853 |
| 10M | 0.927 |
| 25M | 0.927 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.000 |
| 10M | 0.000 |
| 25M | 0.000 |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 10.6 |
| 10M | 8.0 |
| 25M | 7.5 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 11.1 |
| 10M | 11.1 |
| 25M | 11.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 1.529 |
| 10M | 1.506 |
| 25M | 1.455 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.272 |
| 10M | 0.085 |
| 25M | 0.054 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 1.002 |
| 10M | 1.002 |
| 25M | 1.001 |

### dense / seq / update

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 8.0 |
| 10M | 8.2 |
| 25M | 8.2 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 25.1 |
| 10M | 25.1 |
| 25M | 25.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.018 |
| 10M | 0.016 |
| 25M | 0.019 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.907 |
| 10M | 0.916 |
| 25M | 0.917 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.015 |
| 10M | 0.016 |
| 25M | 0.016 |

### dense / rand / insert

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 11.1 |
| 10M | 13.0 |
| 25M | 13.2 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 20.1 |
| 10M | 20.1 |
| 25M | 20.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.803 |
| 10M | 1.019 |
| 25M | 1.090 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.881 |
| 10M | 0.985 |
| 25M | 0.995 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.895 |
| 10M | 0.989 |
| 25M | 0.995 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 12.9 |
| 10M | 13.7 |
| 25M | 13.9 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 57.1 |
| 10M | 57.1 |
| 25M | 57.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 1.824 |
| 10M | 2.010 |
| 25M | 2.131 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 1.588 |
| 10M | 2.015 |
| 25M | 2.023 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.895 |
| 10M | 0.989 |
| 25M | 0.995 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 11.4 |
| 10M | 8.1 |
| 25M | 7.4 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 11.1 |
| 10M | 11.1 |
| 25M | 11.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 1.524 |
| 10M | 1.496 |
| 25M | 1.439 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.338 |
| 10M | 0.094 |
| 25M | 0.055 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 1.002 |
| 10M | 1.000 |
| 25M | 1.000 |

### dense / rand / update

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 12.5 |
| 10M | 13.3 |
| 25M | 13.5 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 25.1 |
| 10M | 25.1 |
| 25M | 25.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.802 |
| 10M | 1.014 |
| 25M | 1.087 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.956 |
| 10M | 1.001 |
| 25M | 1.002 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.895 |
| 10M | 0.989 |
| 25M | 0.995 |
