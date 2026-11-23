# DAT scale experiment 1: Flat array at scale (reference)

How does the flat array (slot = key) behave from 1M to 100M keys? Every later result is compared with this.

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:06:48 |
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
| Repetitions | 2 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.29 GiB |
| Peak process memory measured | 1.49 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory; that computation is included in every number (see "Key generation cost").

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
| O(1): instructions/op flat across key counts (flat) | max/min ≤ 1.10 | ❌ FAIL | 1.19 | dense / rand / insert |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 1.49 GiB | - |

## Key generation cost (included in every time below)

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.5 | 9.3 |

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
| 1M | 8.1 |
| 10M | 8.3 |
| 25M | 8.4 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 28.0 |
| 10M | 28.0 |
| 25M | 28.0 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.002 |
| 10M | 0.002 |
| 25M | 0.002 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.894 |
| 10M | 0.936 |
| 25M | 0.937 |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 4.8 |
| 10M | 4.7 |
| 25M | 4.8 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 58.0 |
| 10M | 58.0 |
| 25M | 58.0 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.035 |
| 10M | 0.030 |
| 25M | 0.031 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.853 |
| 10M | 0.919 |
| 25M | 0.919 |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 10.7 |
| 10M | 8.4 |
| 25M | 7.9 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 39.1 |
| 10M | 40.2 |
| 25M | 42.4 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 1.435 |
| 10M | 1.412 |
| 25M | 1.361 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.258 |
| 10M | 0.094 |
| 25M | 0.054 |

### dense / seq / update

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 8.3 |
| 10M | 8.5 |
| 25M | 8.6 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 31.0 |
| 10M | 31.0 |
| 25M | 31.0 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.002 |
| 10M | 0.002 |
| 25M | 0.002 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.903 |
| 10M | 0.916 |
| 25M | 0.916 |

### dense / rand / insert

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 12.2 |
| 10M | 14.3 |
| 25M | 14.1 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 50.8 |
| 10M | 60.3 |
| 25M | 55.3 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.757 |
| 10M | 0.973 |
| 25M | 1.032 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.914 |
| 10M | 0.985 |
| 25M | 0.995 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 19.4 |
| 10M | 23.9 |
| 25M | 22.1 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 80.8 |
| 10M | 90.3 |
| 25M | 85.3 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 1.803 |
| 10M | 1.986 |
| 25M | 2.099 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 1.559 |
| 10M | 1.960 |
| 25M | 1.978 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 15.0 |
| 10M | 15.4 |
| 25M | 12.8 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 61.9 |
| 10M | 72.5 |
| 25M | 69.7 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 1.440 |
| 10M | 1.417 |
| 25M | 1.363 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.341 |
| 10M | 0.100 |
| 25M | 0.057 |

### dense / rand / update

Time per operation (ns/op)

| keys | flat |
|---|---:|
| 1M | 13.3 |
| 10M | 14.7 |
| 25M | 14.5 |

Instructions per operation

| keys | flat |
|---|---:|
| 1M | 52.8 |
| 10M | 62.3 |
| 25M | 57.3 |

L1D load misses per operation

| keys | flat |
|---|---:|
| 1M | 0.758 |
| 10M | 0.974 |
| 25M | 1.033 |

LLC misses per operation

| keys | flat |
|---|---:|
| 1M | 0.955 |
| 10M | 0.999 |
| 25M | 1.002 |
