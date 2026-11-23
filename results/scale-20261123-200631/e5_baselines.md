# DAT scale experiment 5: Baselines at scale

Do the hash table and the skip list keep their relative speed at 10M-100M keys? (Flat / rows: experiments 1-2.)

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:10:38 |
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
| Structures | hash, skiplist |
| Pages | base |
| Key counts | 1M, 10M, 25M (key range 0 .. 2^27-1 = 134,217,728 keys) |
| Patterns | dense |
| Orders | seq, rand |
| Operations | insert, get_hit, get_miss, update |
| Repetitions | 2 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.28 GiB |
| Peak process memory measured | 2.00 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory; that computation is included in every number (see "Key generation cost").

## Memory plan

Computed before anything was allocated. Memory = structure + 256 MiB for the program.

| keys | pattern | structure | memory | decision |
|---|---|---|---:|---|
| 1M | dense | hash | 0.38 GiB | ✅ ran: fits: 0.38 GiB of 9.00 GiB |
| 1M | dense | skiplist | 0.34 GiB | ✅ ran: fits: 0.34 GiB of 9.00 GiB |
| 10M | dense | hash | 1.25 GiB | ✅ ran: fits: 1.25 GiB of 9.00 GiB |
| 10M | dense | skiplist | 1.14 GiB | ✅ ran: fits: 1.14 GiB of 9.00 GiB |
| 25M | dense | hash | 2.25 GiB | ✅ ran: fits: 2.25 GiB of 9.00 GiB |
| 25M | dense | skiplist | 2.49 GiB | ⏭ skipped: skip list limited to 10000000 keys (run time) |

## Checks

| Check | Target | Result | Worst | Where |
|---|---|---|---|---|
| Correctness (every get returned its own key) | all ok | ✅ PASS | - | - |
| O(1): instructions/op flat across key counts (hash) | max/min ≤ 1.10 | ❌ FAIL | 1.74 | dense / seq / get_miss |
| O(1): instructions/op flat across key counts (skiplist) | max/min ≤ 1.10 | ❌ FAIL | 1.57 | dense / seq / get_miss |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 2.00 GiB | - |

## Key generation cost (included in every time below)

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.5 | 9.2 |

## Memory per key

Bytes of memory holding data, per key (ran) or needed (skipped). A 64 B node is the minimum.

| keys | pattern | hash | skiplist |
|---|---|---:|---:|
| 1M | dense | 134 | 96 |
| 10M | dense | 107 | 96 |
| 25M | dense | 86 | 96 (skipped) |

## Results

Rows = key count, columns = structure. Medians per operation.

### dense / seq / insert

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 32.2 | 73.7 |
| 10M | 42.5 | 79.0 |
| 25M | 66.8 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 98.5 | 420.8 |
| 10M | 101.8 | 476.4 |
| 25M | 110.3 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.341 | 0.011 |
| 10M | 3.115 | 0.011 |
| 25M | 5.009 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 3.761 | 0.185 |
| 10M | 6.326 | 0.219 |
| 25M | 11.274 | skipped |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 32.6 | 67.1 |
| 10M | 46.8 | 68.4 |
| 25M | 52.0 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 112.5 | 363.5 |
| 10M | 115.8 | 415.2 |
| 25M | 115.8 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.660 | 0.492 |
| 10M | 3.652 | 0.471 |
| 25M | 4.109 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.795 | 0.384 |
| 10M | 8.518 | 0.425 |
| 25M | 9.461 | skipped |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 49.1 | 15.9 |
| 10M | 103.1 | 28.5 |
| 25M | 185.4 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 98.1 | 225.1 |
| 10M | 113.8 | 354.2 |
| 25M | 170.8 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.431 | 0.000 |
| 10M | 8.225 | 0.000 |
| 25M | 13.731 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 8.979 | 0.000 |
| 10M | 21.336 | 0.000 |
| 25M | 35.277 | skipped |

### dense / seq / update

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 34.3 | 69.4 |
| 10M | 52.0 | 70.3 |
| 25M | 58.6 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 100.5 | 387.5 |
| 10M | 103.8 | 443.2 |
| 25M | 103.8 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.724 | 0.463 |
| 10M | 3.873 | 0.414 |
| 25M | 4.446 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.858 | 0.378 |
| 10M | 9.091 | 0.418 |
| 25M | 10.307 | skipped |

### dense / rand / insert

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 37.4 | 575.9 |
| 10M | 54.7 | 1444.1 |
| 25M | 75.9 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 121.2 | 455.5 |
| 10M | 133.7 | 522.9 |
| 25M | 137.4 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.375 | 60.269 |
| 10M | 3.355 | 88.738 |
| 25M | 5.356 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 3.754 | 11.263 |
| 10M | 6.327 | 46.922 |
| 25M | 11.204 | skipped |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 39.2 | 730.0 |
| 10M | 59.0 | 1543.8 |
| 25M | 59.9 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 135.3 | 389.5 |
| 10M | 148.1 | 456.3 |
| 25M | 143.1 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.766 | 70.037 |
| 10M | 4.003 | 96.326 |
| 25M | 4.281 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.878 | 20.827 |
| 10M | 8.457 | 60.281 |
| 25M | 9.201 | skipped |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 57.6 | 58.9 |
| 10M | 118.1 | 54.5 |
| 25M | 194.8 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 120.8 | 517.8 |
| 10M | 145.7 | 542.1 |
| 25M | 197.8 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.643 | 0.000 |
| 10M | 9.749 | 0.000 |
| 25M | 14.590 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 9.068 | 0.000 |
| 10M | 21.516 | 0.000 |
| 25M | 35.172 | skipped |

### dense / rand / update

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 38.9 | 820.7 |
| 10M | 60.4 | 1745.1 |
| 25M | 62.1 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 122.3 | 412.5 |
| 10M | 135.1 | 483.3 |
| 25M | 130.1 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.807 | 71.440 |
| 10M | 4.102 | 97.242 |
| 25M | 4.434 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.828 | 20.807 |
| 10M | 8.567 | 59.503 |
| 25M | 9.517 | skipped |
