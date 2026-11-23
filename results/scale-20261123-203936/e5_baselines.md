# DAT scale experiment 5: Baselines at scale

Do the hash table and the skip list keep their relative speed at 10M-100M keys? (Flat / rows: experiments 1-2.)

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:57:03 |
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
| Key counts | 1M, 10M, 25M, 50M, 75M, 100M (key range 0 .. 2^27-1 = 134,217,728 keys) |
| Patterns | dense |
| Orders | seq, rand |
| Operations | insert, get_hit, get_miss, update |
| Repetitions | 3 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.39 GiB |
| Peak process memory measured | 8.00 GiB |
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
| 50M | dense | hash | 4.25 GiB | ✅ ran: fits: 4.25 GiB of 9.00 GiB |
| 50M | dense | skiplist | 4.72 GiB | ⏭ skipped: skip list limited to 10000000 keys (run time) |
| 75M | dense | hash | 8.25 GiB | ✅ ran: fits: 8.25 GiB of 9.00 GiB |
| 75M | dense | skiplist | 6.96 GiB | ⏭ skipped: skip list limited to 10000000 keys (run time) |
| 100M | dense | hash | 8.25 GiB | ✅ ran: fits: 8.25 GiB of 9.00 GiB |
| 100M | dense | skiplist | 9.19 GiB | ⏭ skipped: skip list limited to 10000000 keys (run time) |

## Checks

| Check | Target | Result | Worst | Where |
|---|---|---|---|---|
| Correctness (every get returned its own key) | all ok | ✅ PASS | - | - |
| O(1): instructions/op flat across key counts (hash) | max/min ≤ 1.10 | ❌ FAIL | 2.02 | dense / seq / get_miss |
| O(1): instructions/op flat across key counts (skiplist) | max/min ≤ 1.10 | ❌ FAIL | 1.66 | dense / seq / get_miss |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 8.00 GiB | - |

## Key generation cost (NOT included in the measurements)

Keys are computed in batches of 4096 between measured sections; the timer and counters see only the structure work. For information, computing a key costs:

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.4 | 9.6 |

## Memory per key

Bytes of memory holding data, per key (ran) or needed (skipped). A 64 B node is the minimum.

| keys | pattern | hash | skiplist |
|---|---|---:|---:|
| 1M | dense | 134 | 96 |
| 10M | dense | 107 | 96 |
| 25M | dense | 86 | 96 (skipped) |
| 50M | dense | 86 | 96 (skipped) |
| 75M | dense | 115 | 96 (skipped) |
| 100M | dense | 86 | 96 (skipped) |

## Results

Rows = key count, columns = structure. Medians per operation.

### dense / seq / insert

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 31.4 | 74.7 |
| 10M | 42.9 | 80.7 |
| 25M | 67.7 | skipped |
| 50M | 73.3 | skipped |
| 75M | 48.0 | skipped |
| 100M | 79.7 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 91.5 | 413.8 |
| 10M | 94.9 | 469.5 |
| 25M | 103.4 | skipped |
| 50M | 103.3 | skipped |
| 75M | 93.4 | skipped |
| 100M | 103.0 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.503 | 0.018 |
| 10M | 3.349 | 0.019 |
| 25M | 5.240 | skipped |
| 50M | 5.728 | skipped |
| 75M | 3.721 | skipped |
| 100M | 6.029 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.141 | 0.185 |
| 10M | 6.963 | 0.217 |
| 25M | 11.885 | skipped |
| 50M | 12.523 | skipped |
| 75M | 7.343 | skipped |
| 100M | 13.305 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.958 | 0.012 |
| 10M | 1.029 | 0.012 |
| 25M | 1.073 | skipped |
| 50M | 1.077 | skipped |
| 75M | 1.026 | skipped |
| 100M | 1.079 | skipped |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 31.0 | 80.6 |
| 10M | 45.7 | 80.2 |
| 25M | 50.4 | skipped |
| 50M | 29.3 | skipped |
| 75M | 32.6 | skipped |
| 100M | 32.6 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 110.5 | 361.6 |
| 10M | 113.9 | 413.2 |
| 25M | 113.8 | skipped |
| 50M | 107.6 | skipped |
| 75M | 106.1 | skipped |
| 100M | 106.1 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.678 | 0.503 |
| 10M | 3.710 | 0.492 |
| 25M | 4.162 | skipped |
| 50M | 2.554 | skipped |
| 75M | 2.724 | skipped |
| 100M | 2.723 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.845 | 0.392 |
| 10M | 8.656 | 0.434 |
| 25M | 9.546 | skipped |
| 50M | 3.652 | skipped |
| 75M | 3.361 | skipped |
| 100M | 3.367 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.961 | 0.013 |
| 10M | 1.036 | 0.013 |
| 25M | 1.042 | skipped |
| 50M | 1.003 | skipped |
| 75M | 1.002 | skipped |
| 100M | 1.001 | skipped |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 43.1 | 16.3 |
| 10M | 105.6 | 25.6 |
| 25M | 181.9 | skipped |
| 50M | 195.5 | skipped |
| 75M | 105.7 | skipped |
| 100M | 205.5 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 68.1 | 195.1 |
| 10M | 82.7 | 323.1 |
| 25M | 137.5 | skipped |
| 50M | 137.2 | skipped |
| 75M | 76.0 | skipped |
| 100M | 135.9 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.314 | 0.001 |
| 10M | 7.962 | 0.002 |
| 25M | 13.176 | skipped |
| 50M | 14.521 | skipped |
| 75M | 8.215 | skipped |
| 100M | 15.368 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 9.662 | 0.000 |
| 10M | 23.277 | 0.000 |
| 25M | 35.852 | skipped |
| 50M | 36.846 | skipped |
| 75M | 22.547 | skipped |
| 100M | 37.312 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 1.001 | 0.000 |
| 10M | 1.174 | 0.000 |
| 25M | 1.253 | skipped |
| 50M | 1.267 | skipped |
| 75M | 1.145 | skipped |
| 100M | 1.277 | skipped |

### dense / seq / update

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 32.5 | 72.6 |
| 10M | 49.3 | 73.4 |
| 25M | 54.8 | skipped |
| 50M | 30.4 | skipped |
| 75M | 34.0 | skipped |
| 100M | 34.1 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 94.5 | 381.6 |
| 10M | 97.9 | 437.3 |
| 25M | 97.8 | skipped |
| 50M | 91.6 | skipped |
| 75M | 90.1 | skipped |
| 100M | 90.1 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.651 | 0.509 |
| 10M | 3.772 | 0.467 |
| 25M | 4.275 | skipped |
| 50M | 2.531 | skipped |
| 75M | 2.657 | skipped |
| 100M | 2.656 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.667 | 0.392 |
| 10M | 8.729 | 0.430 |
| 25M | 9.692 | skipped |
| 50M | 3.431 | skipped |
| 75M | 3.057 | skipped |
| 100M | 3.075 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.960 | 0.013 |
| 10M | 1.038 | 0.013 |
| 25M | 1.046 | skipped |
| 50M | 1.002 | skipped |
| 75M | 1.002 | skipped |
| 100M | 1.001 | skipped |

### dense / rand / insert

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 31.3 | 544.0 |
| 10M | 42.6 | 1303.0 |
| 25M | 67.3 | skipped |
| 50M | 72.9 | skipped |
| 75M | 47.7 | skipped |
| 100M | 79.2 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 91.5 | 425.8 |
| 10M | 94.9 | 484.0 |
| 25M | 103.4 | skipped |
| 50M | 103.3 | skipped |
| 75M | 93.4 | skipped |
| 100M | 103.0 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.492 | 59.952 |
| 10M | 3.323 | 89.311 |
| 25M | 5.207 | skipped |
| 50M | 5.696 | skipped |
| 75M | 3.694 | skipped |
| 100M | 5.987 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.110 | 11.450 |
| 10M | 6.900 | 47.463 |
| 25M | 11.824 | skipped |
| 50M | 12.458 | skipped |
| 75M | 7.295 | skipped |
| 100M | 13.230 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.957 | 12.340 |
| 10M | 1.028 | 25.497 |
| 25M | 1.072 | skipped |
| 50M | 1.076 | skipped |
| 75M | 1.025 | skipped |
| 100M | 1.078 | skipped |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 31.0 | 741.7 |
| 10M | 45.2 | 1496.8 |
| 25M | 50.3 | skipped |
| 50M | 29.4 | skipped |
| 75M | 32.6 | skipped |
| 100M | 32.6 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 110.5 | 364.8 |
| 10M | 113.9 | 422.1 |
| 25M | 113.8 | skipped |
| 50M | 107.6 | skipped |
| 75M | 106.1 | skipped |
| 100M | 106.1 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.689 | 72.076 |
| 10M | 3.671 | 98.139 |
| 25M | 4.163 | skipped |
| 50M | 2.561 | skipped |
| 75M | 2.725 | skipped |
| 100M | 2.726 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.869 | 22.166 |
| 10M | 8.526 | 61.575 |
| 25M | 9.536 | skipped |
| 50M | 3.680 | skipped |
| 75M | 3.394 | skipped |
| 100M | 3.363 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.960 | 18.637 |
| 10M | 1.036 | 28.892 |
| 25M | 1.042 | skipped |
| 50M | 1.002 | skipped |
| 75M | 1.001 | skipped |
| 100M | 1.001 | skipped |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 42.9 | 53.3 |
| 10M | 105.5 | 44.8 |
| 25M | 181.6 | skipped |
| 50M | 195.6 | skipped |
| 75M | 107.3 | skipped |
| 100M | 205.1 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 68.1 | 465.1 |
| 10M | 82.7 | 479.1 |
| 25M | 137.5 | skipped |
| 50M | 137.1 | skipped |
| 75M | 76.0 | skipped |
| 100M | 136.0 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.262 | 0.005 |
| 10M | 7.921 | 0.003 |
| 25M | 13.170 | skipped |
| 50M | 14.540 | skipped |
| 75M | 8.121 | skipped |
| 100M | 15.329 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 9.483 | 0.000 |
| 10M | 23.210 | 0.000 |
| 25M | 35.844 | skipped |
| 50M | 36.875 | skipped |
| 75M | 22.376 | skipped |
| 100M | 37.278 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 1.000 | 0.000 |
| 10M | 1.175 | 0.000 |
| 25M | 1.254 | skipped |
| 50M | 1.267 | skipped |
| 75M | 1.150 | skipped |
| 100M | 1.276 | skipped |

### dense / rand / update

Time per operation (ns/op)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 32.6 | 787.9 |
| 10M | 47.7 | 1657.2 |
| 25M | 53.3 | skipped |
| 50M | 30.4 | skipped |
| 75M | 34.1 | skipped |
| 100M | 34.2 | skipped |

Instructions per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 94.5 | 384.8 |
| 10M | 97.9 | 446.1 |
| 25M | 97.8 | skipped |
| 50M | 91.6 | skipped |
| 75M | 90.1 | skipped |
| 100M | 90.1 | skipped |

L1D load misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 2.662 | 71.534 |
| 10M | 3.658 | 98.103 |
| 25M | 4.180 | skipped |
| 50M | 2.532 | skipped |
| 75M | 2.657 | skipped |
| 100M | 2.659 | skipped |

LLC misses per operation

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 4.718 | 20.780 |
| 10M | 8.335 | 59.919 |
| 25M | 9.452 | skipped |
| 50M | 3.437 | skipped |
| 75M | 3.082 | skipped |
| 100M | 3.077 | skipped |

dTLB misses per operation (load + store)

| keys | hash | skiplist |
|---|---:|---:|
| 1M | 0.960 | 18.436 |
| 10M | 1.036 | 28.624 |
| 25M | 1.044 | skipped |
| 50M | 1.003 | skipped |
| 75M | 1.001 | skipped |
| 100M | 1.001 | skipped |
