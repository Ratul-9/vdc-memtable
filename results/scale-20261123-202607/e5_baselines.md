# DAT scale experiment 5: Baselines at scale

Do the hash table and the skip list keep their relative speed at 10M-100M keys? (Flat / rows: experiments 1-2.)

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:32:01 |
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
| Repetitions | 3 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.33 GiB |
| Peak process memory measured | 2.00 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory. They are computed in batches of 4096 outside the measured sections, so the times and counters below contain only the structure work.

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
| O(1): instructions/op flat across key counts (hash) | max/min ≤ 1.10 | ❌ FAIL | 2.02 | dense / seq / get_miss |
| O(1): instructions/op flat across key counts (skiplist) | max/min ≤ 1.10 | ❌ FAIL | 1.66 | dense / seq / get_miss |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 2.00 GiB | - |

## Key generation cost (NOT included in the measurements)

Keys are computed in batches of 4096 between measured sections; the timer and counters see only the structure work. For information, computing a key costs:

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.4 | 9.5 |

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
| 1M | 31.3 | 74.8 |
| 10M | 42.7 | 80.7 |
| 25M | 67.0 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 91.5 | 413.8 |
| 10M | 94.9 | 469.5 |
| 25M | 103.4 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.495 | 0.018 |
| 10M | 3.338 | 0.020 |
| 25M | 5.205 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.128 | 0.183 |
| 10M | 6.916 | 0.217 |
| 25M | 11.802 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.958 | 0.012 |
| 10M | 1.029 | 0.012 |
| 25M | 1.072 | skipped |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 30.9 | 79.7 |
| 10M | 45.4 | 79.5 |
| 25M | 50.3 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 110.5 | 361.6 |
| 10M | 113.9 | 413.2 |
| 25M | 113.8 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.702 | 0.507 |
| 10M | 3.699 | 0.493 |
| 25M | 4.125 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.898 | 0.391 |
| 10M | 8.621 | 0.438 |
| 25M | 9.480 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.961 | 0.013 |
| 10M | 1.036 | 0.013 |
| 25M | 1.042 | skipped |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 42.9 | 16.2 |
| 10M | 105.5 | 25.6 |
| 25M | 182.0 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 68.1 | 195.1 |
| 10M | 82.7 | 323.1 |
| 25M | 137.5 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.276 | 0.001 |
| 10M | 7.955 | 0.002 |
| 25M | 13.216 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 9.534 | 0.000 |
| 10M | 23.346 | 0.000 |
| 25M | 35.922 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 1.001 | 0.000 |
| 10M | 1.176 | 0.000 |
| 25M | 1.254 | skipped |

### dense / seq / update

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 32.5 | 72.7 |
| 10M | 49.5 | 72.3 |
| 25M | 55.1 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 94.5 | 381.6 |
| 10M | 97.9 | 437.3 |
| 25M | 97.8 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.665 | 0.506 |
| 10M | 3.792 | 0.457 |
| 25M | 4.300 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.695 | 0.388 |
| 10M | 8.770 | 0.428 |
| 25M | 9.739 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.961 | 0.013 |
| 10M | 1.039 | 0.013 |
| 25M | 1.047 | skipped |

### dense / rand / insert

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 31.3 | 545.2 |
| 10M | 42.3 | 1303.3 |
| 25M | 66.7 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 91.5 | 425.8 |
| 10M | 94.9 | 484.0 |
| 25M | 103.4 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.480 | 60.005 |
| 10M | 3.308 | 89.199 |
| 25M | 5.172 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.090 | 11.443 |
| 10M | 6.838 | 47.656 |
| 25M | 11.745 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.958 | 12.430 |
| 10M | 1.027 | 25.481 |
| 25M | 1.072 | skipped |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 30.9 | 730.4 |
| 10M | 45.2 | 1499.7 |
| 25M | 50.2 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 110.5 | 364.8 |
| 10M | 113.9 | 422.1 |
| 25M | 113.8 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.684 | 71.923 |
| 10M | 3.674 | 99.017 |
| 25M | 4.132 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.863 | 22.257 |
| 10M | 8.537 | 62.195 |
| 25M | 9.475 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.961 | 18.712 |
| 10M | 1.036 | 29.145 |
| 25M | 1.042 | skipped |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 42.5 | 53.7 |
| 10M | 105.2 | 46.1 |
| 25M | 181.8 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 68.1 | 465.1 |
| 10M | 82.7 | 479.1 |
| 25M | 137.5 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.307 | 0.005 |
| 10M | 7.900 | 0.004 |
| 25M | 13.201 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 9.637 | 0.000 |
| 10M | 23.199 | 0.000 |
| 25M | 35.897 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 1.000 | 0.000 |
| 10M | 1.175 | 0.000 |
| 25M | 1.253 | skipped |

### dense / rand / update

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 32.4 | 794.8 |
| 10M | 47.5 | 1654.6 |
| 25M | 53.5 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 94.5 | 384.8 |
| 10M | 97.9 | 446.1 |
| 25M | 97.8 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.646 | 71.476 |
| 10M | 3.639 | 97.985 |
| 25M | 4.194 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.635 | 20.981 |
| 10M | 8.272 | 60.234 |
| 25M | 9.479 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.961 | 18.457 |
| 10M | 1.036 | 28.635 |
| 25M | 1.044 | skipped |
