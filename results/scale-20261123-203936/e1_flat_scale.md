# DAT scale experiment 1: Flat array at scale (reference)

How does the flat array (slot = key) behave from 1M to 100M keys? Every later result is compared with this.

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:40:50 |
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
| Key counts | 1M, 10M, 25M, 50M, 75M, 100M (key range 0 .. 2^27-1 = 134,217,728 keys) |
| Patterns | dense |
| Orders | seq, rand |
| Operations | insert, get_hit, get_miss, update |
| Repetitions | 3 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.35 GiB |
| Peak process memory measured | 5.96 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory. They are computed in batches of 4096 outside the measured sections, so the times and counters below contain only the structure work.

## Memory plan

Computed before anything was allocated. Memory = structure + 256 MiB for the program.

| keys | pattern | structure | memory | decision |
|---|---|---|---:|---|
| 1M | dense | flat | 0.31 GiB | ✅ ran: fits: 0.31 GiB of 9.00 GiB |
| 10M | dense | flat | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 25M | dense | flat | 1.74 GiB | ✅ ran: fits: 1.74 GiB of 9.00 GiB |
| 50M | dense | flat | 3.23 GiB | ✅ ran: fits: 3.23 GiB of 9.00 GiB |
| 75M | dense | flat | 4.72 GiB | ✅ ran: fits: 4.72 GiB of 9.00 GiB |
| 100M | dense | flat | 6.21 GiB | ✅ ran: fits: 6.21 GiB of 9.00 GiB |

## Checks

| Check | Target | Result | Worst | Where |
|---|---|---|---|---|
| Correctness (every get returned its own key) | all ok | ✅ PASS | - | - |
| O(1): instructions/op flat across key counts (flat) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / rand / get_miss |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 5.96 GiB | - |

## Key generation cost (NOT included in the measurements)

Keys are computed in batches of 4096 between measured sections; the timer and counters see only the structure work. For information, computing a key costs:

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.3 | 9.1 |

## Memory per key

Bytes of memory holding data, per key (ran) or needed (skipped). A 64 B node is the minimum.

| keys | pattern | flat |
|---|---|---:|
| 1M | dense | 64 |
| 10M | dense | 64 |
| 25M | dense | 64 |
| 50M | dense | 64 |
| 75M | dense | 64 |
| 100M | dense | 64 |

## Results

Rows = key count, columns = structure. Medians per operation.

### dense / seq / insert

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 7.6 |
| 10M | 8.0 |
| 25M | 8.0 |
| 50M | 7.9 |
| 75M | 7.9 |
| 100M | 8.0 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 20.1 |
| 10M | 20.1 |
| 25M | 20.1 |
| 50M | 20.1 |
| 75M | 20.1 |
| 100M | 20.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.012 |
| 10M | 0.012 |
| 25M | 0.012 |
| 50M | 0.012 |
| 75M | 0.012 |
| 100M | 0.011 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.891 |
| 10M | 0.938 |
| 25M | 0.938 |
| 50M | 0.936 |
| 75M | 0.934 |
| 100M | 0.935 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.015 |
| 10M | 0.016 |
| 25M | 0.016 |
| 50M | 0.016 |
| 75M | 0.016 |
| 100M | 0.016 |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 4.6 |
| 10M | 4.5 |
| 25M | 4.5 |
| 50M | 4.5 |
| 75M | 4.5 |
| 100M | 4.5 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 57.1 |
| 10M | 57.1 |
| 25M | 57.1 |
| 50M | 57.1 |
| 75M | 57.1 |
| 100M | 57.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.252 |
| 10M | 0.204 |
| 25M | 0.202 |
| 50M | 0.217 |
| 75M | 0.216 |
| 100M | 0.202 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.858 |
| 10M | 0.930 |
| 25M | 0.931 |
| 50M | 0.929 |
| 75M | 0.929 |
| 100M | 0.931 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.000 |
| 10M | 0.000 |
| 25M | 0.000 |
| 50M | 0.000 |
| 75M | 0.000 |
| 100M | 0.000 |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 10.4 |
| 10M | 7.9 |
| 25M | 7.3 |
| 50M | 6.7 |
| 75M | 6.1 |
| 100M | 5.5 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 11.1 |
| 10M | 11.1 |
| 25M | 11.1 |
| 50M | 11.1 |
| 75M | 11.1 |
| 100M | 11.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 1.530 |
| 10M | 1.503 |
| 25M | 1.449 |
| 50M | 1.351 |
| 75M | 1.215 |
| 100M | 1.072 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.276 |
| 10M | 0.089 |
| 25M | 0.051 |
| 50M | 0.019 |
| 75M | 0.008 |
| 100M | 0.003 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 1.002 |
| 10M | 1.001 |
| 25M | 1.001 |
| 50M | 1.000 |
| 75M | 1.000 |
| 100M | 0.998 |

### dense / seq / update

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 8.0 |
| 10M | 8.2 |
| 25M | 8.2 |
| 50M | 8.2 |
| 75M | 8.2 |
| 100M | 8.2 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 25.1 |
| 10M | 25.1 |
| 25M | 25.1 |
| 50M | 25.1 |
| 75M | 25.1 |
| 100M | 25.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.015 |
| 10M | 0.015 |
| 25M | 0.015 |
| 50M | 0.015 |
| 75M | 0.015 |
| 100M | 0.015 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.908 |
| 10M | 0.918 |
| 25M | 0.918 |
| 50M | 0.916 |
| 75M | 0.916 |
| 100M | 0.918 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.015 |
| 10M | 0.016 |
| 25M | 0.016 |
| 50M | 0.016 |
| 75M | 0.016 |
| 100M | 0.016 |

### dense / rand / insert

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 11.3 |
| 10M | 13.0 |
| 25M | 13.3 |
| 50M | 13.4 |
| 75M | 13.7 |
| 100M | 14.0 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 20.1 |
| 10M | 20.1 |
| 25M | 20.1 |
| 50M | 20.1 |
| 75M | 20.1 |
| 100M | 20.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.805 |
| 10M | 1.017 |
| 25M | 1.088 |
| 50M | 1.273 |
| 75M | 1.428 |
| 100M | 1.535 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.889 |
| 10M | 0.986 |
| 25M | 0.995 |
| 50M | 1.002 |
| 75M | 1.011 |
| 100M | 1.038 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.895 |
| 10M | 0.989 |
| 25M | 0.995 |
| 50M | 0.999 |
| 75M | 0.999 |
| 100M | 0.999 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 13.0 |
| 10M | 13.6 |
| 25M | 13.9 |
| 50M | 14.5 |
| 75M | 16.9 |
| 100M | 18.7 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 57.1 |
| 10M | 57.1 |
| 25M | 57.1 |
| 50M | 57.1 |
| 75M | 57.1 |
| 100M | 57.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 1.827 |
| 10M | 2.006 |
| 25M | 2.129 |
| 50M | 2.363 |
| 75M | 2.515 |
| 100M | 2.620 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 1.603 |
| 10M | 1.997 |
| 25M | 2.036 |
| 50M | 2.054 |
| 75M | 2.233 |
| 100M | 2.497 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.895 |
| 10M | 0.990 |
| 25M | 0.995 |
| 50M | 0.998 |
| 75M | 0.999 |
| 100M | 0.999 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 11.1 |
| 10M | 8.0 |
| 25M | 7.3 |
| 50M | 6.7 |
| 75M | 5.9 |
| 100M | 5.7 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 11.1 |
| 10M | 11.1 |
| 25M | 11.1 |
| 50M | 11.1 |
| 75M | 11.1 |
| 100M | 11.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 1.531 |
| 10M | 1.496 |
| 25M | 1.437 |
| 50M | 1.333 |
| 75M | 1.205 |
| 100M | 1.072 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.343 |
| 10M | 0.098 |
| 25M | 0.057 |
| 50M | 0.021 |
| 75M | 0.010 |
| 100M | 0.004 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 1.001 |
| 10M | 1.001 |
| 25M | 1.000 |
| 50M | 0.999 |
| 75M | 0.999 |
| 100M | 0.997 |

### dense / rand / update

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 12.6 |
| 10M | 13.4 |
| 25M | 13.5 |
| 50M | 13.6 |
| 75M | 13.8 |
| 100M | 14.2 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 25.1 |
| 10M | 25.1 |
| 25M | 25.1 |
| 50M | 25.1 |
| 75M | 25.1 |
| 100M | 25.1 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.803 |
| 10M | 1.013 |
| 25M | 1.085 |
| 50M | 1.270 |
| 75M | 1.426 |
| 100M | 1.534 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.969 |
| 10M | 1.000 |
| 25M | 1.002 |
| 50M | 1.005 |
| 75M | 1.014 |
| 100M | 1.043 |

dTLB misses per operation (load + store)

| keys | flat |
|---|---:|
| 1M | 0.895 |
| 10M | 0.989 |
| 25M | 0.994 |
| 50M | 0.998 |
| 75M | 0.999 |
| 100M | 0.999 |
