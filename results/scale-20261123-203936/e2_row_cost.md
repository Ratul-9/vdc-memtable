# DAT scale experiment 2: Cost of the row table (two-level vs flat)

How much slower is the two-level layout than the flat array when its row table lives in L1 (2 MiB rows), L2 (64 KiB rows) or L3 (4 KiB rows)?

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:45:21 |
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
| Key counts | 1M, 10M, 25M, 50M, 75M, 100M (key range 0 .. 2^27-1 = 134,217,728 keys) |
| Patterns | dense |
| Orders | seq, rand |
| Operations | insert, get_hit, get_miss, update |
| Repetitions | 3 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.41 GiB |
| Peak process memory measured | 5.97 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory. They are computed in batches of 4096 outside the measured sections, so the times and counters below contain only the structure work.

## Memory plan

Computed before anything was allocated. Memory = structure + 256 MiB for the program.

| keys | pattern | structure | memory | decision |
|---|---|---|---:|---|
| 1M | dense | flat | 0.31 GiB | ✅ ran: fits: 0.31 GiB of 9.00 GiB |
| 1M | dense | rows-4K | 0.32 GiB | ✅ ran: fits: 0.32 GiB of 9.00 GiB |
| 1M | dense | rows-64K | 0.31 GiB | ✅ ran: fits: 0.31 GiB of 9.00 GiB |
| 1M | dense | rows-2M | 0.31 GiB | ✅ ran: fits: 0.31 GiB of 9.00 GiB |
| 10M | dense | flat | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | dense | rows-4K | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | dense | rows-64K | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 10M | dense | rows-2M | 0.85 GiB | ✅ ran: fits: 0.85 GiB of 9.00 GiB |
| 25M | dense | flat | 1.74 GiB | ✅ ran: fits: 1.74 GiB of 9.00 GiB |
| 25M | dense | rows-4K | 1.75 GiB | ✅ ran: fits: 1.75 GiB of 9.00 GiB |
| 25M | dense | rows-64K | 1.74 GiB | ✅ ran: fits: 1.74 GiB of 9.00 GiB |
| 25M | dense | rows-2M | 1.74 GiB | ✅ ran: fits: 1.74 GiB of 9.00 GiB |
| 50M | dense | flat | 3.23 GiB | ✅ ran: fits: 3.23 GiB of 9.00 GiB |
| 50M | dense | rows-4K | 3.24 GiB | ✅ ran: fits: 3.24 GiB of 9.00 GiB |
| 50M | dense | rows-64K | 3.23 GiB | ✅ ran: fits: 3.23 GiB of 9.00 GiB |
| 50M | dense | rows-2M | 3.23 GiB | ✅ ran: fits: 3.23 GiB of 9.00 GiB |
| 75M | dense | flat | 4.72 GiB | ✅ ran: fits: 4.72 GiB of 9.00 GiB |
| 75M | dense | rows-4K | 4.73 GiB | ✅ ran: fits: 4.73 GiB of 9.00 GiB |
| 75M | dense | rows-64K | 4.72 GiB | ✅ ran: fits: 4.72 GiB of 9.00 GiB |
| 75M | dense | rows-2M | 4.72 GiB | ✅ ran: fits: 4.72 GiB of 9.00 GiB |
| 100M | dense | flat | 6.21 GiB | ✅ ran: fits: 6.21 GiB of 9.00 GiB |
| 100M | dense | rows-4K | 6.22 GiB | ✅ ran: fits: 6.22 GiB of 9.00 GiB |
| 100M | dense | rows-64K | 6.21 GiB | ✅ ran: fits: 6.21 GiB of 9.00 GiB |
| 100M | dense | rows-2M | 6.21 GiB | ✅ ran: fits: 6.21 GiB of 9.00 GiB |

## Checks

| Check | Target | Result | Worst | Where |
|---|---|---|---|---|
| Correctness (every get returned its own key) | all ok | ✅ PASS | - | - |
| O(1): instructions/op flat across key counts (flat) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / seq / get_miss |
| O(1): instructions/op flat across key counts (rows-4K) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / rand / get_miss |
| O(1): instructions/op flat across key counts (rows-64K) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / seq / get_miss |
| O(1): instructions/op flat across key counts (rows-2M) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / rand / get_miss |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 5.97 GiB | - |

## Key generation cost (NOT included in the measurements)

Keys are computed in batches of 4096 between measured sections; the timer and counters see only the structure work. For information, computing a key costs:

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.4 | 9.6 |

## Memory per key

Bytes of memory holding data, per key (ran) or needed (skipped). A 64 B node is the minimum.

| keys | pattern | flat | rows-4K | rows-64K | rows-2M |
|---|---|---:|---:|---:|---:|
| 1M | dense | 64 | 72 | 65 | 65 |
| 10M | dense | 64 | 65 | 64 | 64 |
| 25M | dense | 64 | 64 | 64 | 64 |
| 50M | dense | 64 | 64 | 64 | 64 |
| 75M | dense | 64 | 64 | 64 | 64 |
| 100M | dense | 64 | 64 | 64 | 64 |

## Results

Rows = key count, columns = structure. Medians per operation.

### dense / seq / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 7.6 | 7.4 | 6.6 | 6.9 |
| 10M | 7.9 | 7.9 | 7.9 | 7.9 |
| 25M | 7.9 | 8.0 | 7.9 | 7.9 |
| 50M | 7.9 | 8.0 | 7.9 | 8.0 |
| 75M | 7.9 | 7.9 | 7.9 | 7.9 |
| 100M | 7.9 | 7.9 | 7.9 | 7.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 20.1 | 34.2 | 34.1 | 34.1 |
| 10M | 20.1 | 34.2 | 34.1 | 34.1 |
| 25M | 20.1 | 34.2 | 34.1 | 34.1 |
| 50M | 20.1 | 34.2 | 34.1 | 34.1 |
| 75M | 20.1 | 34.2 | 34.1 | 34.1 |
| 100M | 20.1 | 34.2 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.012 | 0.016 | 0.022 | 0.012 |
| 10M | 0.011 | 0.017 | 0.016 | 0.016 |
| 25M | 0.011 | 0.017 | 0.016 | 0.016 |
| 50M | 0.012 | 0.017 | 0.016 | 0.016 |
| 75M | 0.011 | 0.017 | 0.016 | 0.017 |
| 100M | 0.011 | 0.017 | 0.016 | 0.016 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.894 | 0.878 | 0.765 | 0.780 |
| 10M | 0.934 | 0.938 | 0.935 | 0.935 |
| 25M | 0.933 | 0.938 | 0.936 | 0.937 |
| 50M | 0.933 | 0.939 | 0.937 | 0.938 |
| 75M | 0.932 | 0.938 | 0.936 | 0.937 |
| 100M | 0.931 | 0.937 | 0.936 | 0.935 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.015 | 0.016 | 0.015 | 0.015 |
| 10M | 0.016 | 0.016 | 0.016 | 0.016 |
| 25M | 0.016 | 0.016 | 0.016 | 0.016 |
| 50M | 0.016 | 0.016 | 0.016 | 0.016 |
| 75M | 0.016 | 0.016 | 0.016 | 0.016 |
| 100M | 0.016 | 0.016 | 0.016 | 0.016 |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 4.6 | 5.0 | 4.8 | 4.8 |
| 10M | 4.5 | 4.8 | 4.9 | 4.8 |
| 25M | 4.5 | 4.8 | 4.8 | 4.8 |
| 50M | 4.5 | 4.8 | 4.8 | 4.8 |
| 75M | 4.5 | 4.8 | 4.8 | 4.8 |
| 100M | 4.5 | 4.8 | 4.8 | 4.8 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 57.1 | 73.1 | 73.1 | 73.1 |
| 10M | 57.1 | 73.1 | 73.1 | 73.1 |
| 25M | 57.1 | 73.1 | 73.1 | 73.1 |
| 50M | 57.1 | 73.1 | 73.1 | 73.1 |
| 75M | 57.1 | 73.1 | 73.1 | 73.1 |
| 100M | 57.1 | 73.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.072 | 0.022 | 0.021 | 0.020 |
| 10M | 0.062 | 0.022 | 0.022 | 0.021 |
| 25M | 0.062 | 0.022 | 0.022 | 0.022 |
| 50M | 0.063 | 0.023 | 0.022 | 0.021 |
| 75M | 0.062 | 0.024 | 0.022 | 0.021 |
| 100M | 0.063 | 0.023 | 0.022 | 0.022 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.860 | 0.807 | 0.717 | 0.732 |
| 10M | 0.926 | 0.892 | 0.895 | 0.894 |
| 25M | 0.926 | 0.893 | 0.898 | 0.897 |
| 50M | 0.927 | 0.893 | 0.899 | 0.906 |
| 75M | 0.927 | 0.895 | 0.899 | 0.906 |
| 100M | 0.927 | 0.895 | 0.899 | 0.898 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.000 | 0.000 | 0.000 | 0.000 |
| 10M | 0.000 | 0.000 | 0.000 | 0.000 |
| 25M | 0.000 | 0.000 | 0.000 | 0.000 |
| 50M | 0.000 | 0.000 | 0.000 | 0.000 |
| 75M | 0.000 | 0.000 | 0.000 | 0.000 |
| 100M | 0.000 | 0.000 | 0.000 | 0.000 |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 10.5 | 2.6 | 1.0 | 0.9 |
| 10M | 8.0 | 1.4 | 1.0 | 0.9 |
| 25M | 7.5 | 1.3 | 1.0 | 0.8 |
| 50M | 6.8 | 1.2 | 1.0 | 0.8 |
| 75M | 6.3 | 1.2 | 0.9 | 0.8 |
| 100M | 5.7 | 1.1 | 0.9 | 0.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 11.1 | 16.1 | 16.1 | 16.1 |
| 10M | 11.1 | 16.1 | 16.1 | 16.1 |
| 25M | 11.1 | 16.1 | 16.1 | 16.1 |
| 50M | 11.1 | 16.1 | 16.1 | 16.1 |
| 75M | 11.1 | 16.1 | 16.1 | 16.1 |
| 100M | 11.1 | 16.1 | 16.1 | 16.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.531 | 1.115 | 0.901 | 0.012 |
| 10M | 1.502 | 1.078 | 0.899 | 0.012 |
| 25M | 1.454 | 1.018 | 0.889 | 0.010 |
| 50M | 1.345 | 1.013 | 0.864 | 0.004 |
| 75M | 1.218 | 1.003 | 0.799 | 0.002 |
| 100M | 1.076 | 0.988 | 0.684 | 0.002 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.272 | 0.093 | 0.007 | 0.001 |
| 10M | 0.084 | 0.011 | 0.001 | 0.000 |
| 25M | 0.051 | 0.004 | 0.000 | 0.000 |
| 50M | 0.022 | 0.003 | 0.000 | 0.000 |
| 75M | 0.008 | 0.003 | 0.000 | 0.000 |
| 100M | 0.003 | 0.002 | 0.000 | 0.000 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.002 | 0.137 | 0.000 | 0.000 |
| 10M | 1.002 | 0.074 | 0.000 | 0.000 |
| 25M | 1.001 | 0.003 | 0.000 | 0.000 |
| 50M | 1.001 | 0.000 | 0.000 | 0.000 |
| 75M | 1.000 | 0.000 | 0.000 | 0.000 |
| 100M | 0.998 | 0.000 | 0.000 | 0.000 |

### dense / seq / update

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 8.0 | 7.9 | 7.2 | 7.1 |
| 10M | 8.2 | 8.3 | 8.3 | 8.3 |
| 25M | 8.2 | 8.4 | 8.3 | 8.3 |
| 50M | 8.2 | 8.3 | 8.3 | 8.4 |
| 75M | 8.2 | 8.3 | 8.3 | 8.3 |
| 100M | 8.2 | 8.3 | 8.3 | 8.3 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 25.1 | 40.1 | 40.1 | 40.1 |
| 10M | 25.1 | 40.1 | 40.1 | 40.1 |
| 25M | 25.1 | 40.1 | 40.1 | 40.1 |
| 50M | 25.1 | 40.1 | 40.1 | 40.1 |
| 75M | 25.1 | 40.1 | 40.1 | 40.1 |
| 100M | 25.1 | 40.1 | 40.1 | 40.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.009 | 0.009 | 0.007 | 0.006 |
| 10M | 0.009 | 0.009 | 0.008 | 0.008 |
| 25M | 0.009 | 0.009 | 0.008 | 0.008 |
| 50M | 0.009 | 0.009 | 0.008 | 0.008 |
| 75M | 0.009 | 0.009 | 0.008 | 0.008 |
| 100M | 0.009 | 0.009 | 0.008 | 0.008 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.906 | 0.851 | 0.727 | 0.719 |
| 10M | 0.917 | 0.899 | 0.895 | 0.897 |
| 25M | 0.917 | 0.901 | 0.899 | 0.899 |
| 50M | 0.917 | 0.900 | 0.900 | 0.903 |
| 75M | 0.917 | 0.900 | 0.899 | 0.903 |
| 100M | 0.917 | 0.900 | 0.900 | 0.900 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.016 | 0.016 | 0.015 | 0.015 |
| 10M | 0.016 | 0.016 | 0.016 | 0.016 |
| 25M | 0.016 | 0.016 | 0.016 | 0.016 |
| 50M | 0.016 | 0.016 | 0.016 | 0.016 |
| 75M | 0.016 | 0.016 | 0.016 | 0.016 |
| 100M | 0.016 | 0.016 | 0.016 | 0.016 |

### dense / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 11.1 | 10.9 | 11.1 | 11.1 |
| 10M | 13.0 | 13.4 | 13.0 | 13.0 |
| 25M | 13.2 | 13.9 | 13.4 | 13.3 |
| 50M | 13.4 | 14.3 | 13.7 | 13.5 |
| 75M | 13.6 | 15.4 | 13.9 | 13.7 |
| 100M | 14.0 | 17.7 | 14.4 | 14.2 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 20.1 | 34.2 | 34.1 | 34.1 |
| 10M | 20.1 | 34.2 | 34.1 | 34.1 |
| 25M | 20.1 | 34.2 | 34.1 | 34.1 |
| 50M | 20.1 | 34.2 | 34.1 | 34.1 |
| 75M | 20.1 | 34.2 | 34.1 | 34.1 |
| 100M | 20.1 | 34.2 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.805 | 1.590 | 0.837 | 0.813 |
| 10M | 1.016 | 1.998 | 1.703 | 1.023 |
| 25M | 1.091 | 2.169 | 2.021 | 1.124 |
| 50M | 1.275 | 2.537 | 2.329 | 1.433 |
| 75M | 1.432 | 2.796 | 2.504 | 1.720 |
| 100M | 1.536 | 2.975 | 2.612 | 1.945 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.884 | 0.840 | 0.876 | 0.870 |
| 10M | 0.984 | 0.997 | 0.981 | 0.983 |
| 25M | 0.996 | 1.004 | 0.995 | 0.995 |
| 50M | 1.001 | 1.015 | 1.003 | 1.001 |
| 75M | 1.012 | 1.087 | 1.016 | 1.011 |
| 100M | 1.034 | 1.265 | 1.043 | 1.035 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.896 | 0.903 | 0.900 | 0.900 |
| 10M | 0.988 | 0.992 | 0.990 | 0.989 |
| 25M | 0.994 | 1.030 | 0.995 | 0.995 |
| 50M | 0.998 | 1.210 | 0.999 | 0.999 |
| 75M | 0.999 | 1.377 | 0.999 | 0.999 |
| 100M | 0.999 | 1.498 | 1.000 | 0.999 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 13.3 | 18.4 | 16.9 | 16.8 |
| 10M | 14.2 | 21.4 | 20.7 | 19.1 |
| 25M | 14.5 | 22.7 | 20.6 | 19.6 |
| 50M | 15.1 | 24.4 | 21.8 | 20.1 |
| 75M | 17.6 | 29.0 | 24.1 | 22.1 |
| 100M | 19.3 | 32.7 | 26.3 | 24.3 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 57.1 | 73.1 | 73.1 | 73.1 |
| 10M | 57.1 | 73.1 | 73.1 | 73.1 |
| 25M | 57.1 | 73.1 | 73.1 | 73.1 |
| 50M | 57.1 | 73.1 | 73.1 | 73.1 |
| 75M | 57.1 | 73.1 | 73.1 | 73.1 |
| 100M | 57.1 | 73.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.824 | 2.644 | 1.873 | 1.825 |
| 10M | 2.004 | 2.997 | 2.760 | 2.013 |
| 25M | 2.126 | 3.217 | 3.103 | 2.193 |
| 50M | 2.358 | 3.586 | 3.416 | 2.594 |
| 75M | 2.512 | 3.815 | 3.575 | 2.897 |
| 100M | 2.609 | 3.958 | 3.669 | 3.110 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.586 | 1.645 | 1.604 | 1.594 |
| 10M | 2.015 | 2.024 | 2.007 | 1.998 |
| 25M | 2.054 | 2.047 | 2.040 | 2.027 |
| 50M | 2.063 | 2.171 | 2.076 | 2.052 |
| 75M | 2.270 | 2.722 | 2.256 | 2.204 |
| 100M | 2.526 | 3.219 | 2.530 | 2.467 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.896 | 0.901 | 0.900 | 0.900 |
| 10M | 0.988 | 0.991 | 0.989 | 0.989 |
| 25M | 0.994 | 1.029 | 0.995 | 0.995 |
| 50M | 0.998 | 1.208 | 0.999 | 0.999 |
| 75M | 0.999 | 1.376 | 0.999 | 0.999 |
| 100M | 0.999 | 1.497 | 1.000 | 0.999 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 11.3 | 2.8 | 1.1 | 0.9 |
| 10M | 8.1 | 1.4 | 1.0 | 0.9 |
| 25M | 7.4 | 1.3 | 1.0 | 0.9 |
| 50M | 6.8 | 1.2 | 0.9 | 0.8 |
| 75M | 6.1 | 1.1 | 0.9 | 0.8 |
| 100M | 5.7 | 1.1 | 0.9 | 0.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 11.1 | 16.1 | 16.1 | 16.1 |
| 10M | 11.1 | 16.1 | 16.1 | 16.1 |
| 25M | 11.1 | 16.1 | 16.1 | 16.1 |
| 50M | 11.1 | 16.1 | 16.1 | 16.1 |
| 75M | 11.1 | 16.1 | 16.1 | 16.1 |
| 100M | 11.1 | 16.1 | 16.1 | 16.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.532 | 1.118 | 0.924 | 0.012 |
| 10M | 1.495 | 1.080 | 0.919 | 0.013 |
| 25M | 1.438 | 1.020 | 0.906 | 0.010 |
| 50M | 1.334 | 1.014 | 0.877 | 0.005 |
| 75M | 1.208 | 1.007 | 0.824 | 0.002 |
| 100M | 1.071 | 0.990 | 0.696 | 0.002 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.336 | 0.157 | 0.009 | 0.001 |
| 10M | 0.087 | 0.014 | 0.001 | 0.001 |
| 25M | 0.054 | 0.007 | 0.000 | 0.000 |
| 50M | 0.023 | 0.005 | 0.000 | 0.000 |
| 75M | 0.009 | 0.003 | 0.000 | 0.000 |
| 100M | 0.004 | 0.002 | 0.000 | 0.000 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.002 | 0.138 | 0.000 | 0.000 |
| 10M | 1.000 | 0.074 | 0.000 | 0.000 |
| 25M | 1.000 | 0.003 | 0.000 | 0.000 |
| 50M | 0.999 | 0.000 | 0.000 | 0.000 |
| 75M | 0.999 | 0.000 | 0.000 | 0.000 |
| 100M | 0.997 | 0.000 | 0.000 | 0.000 |

### dense / rand / update

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 12.5 | 11.5 | 10.1 | 10.1 |
| 10M | 13.3 | 13.6 | 13.2 | 13.2 |
| 25M | 13.5 | 14.0 | 13.6 | 13.5 |
| 50M | 13.6 | 14.3 | 13.8 | 13.7 |
| 75M | 13.8 | 15.4 | 14.1 | 13.8 |
| 100M | 14.1 | 17.6 | 14.6 | 14.4 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 25.1 | 40.1 | 40.1 | 40.1 |
| 10M | 25.1 | 40.1 | 40.1 | 40.1 |
| 25M | 25.1 | 40.1 | 40.1 | 40.1 |
| 50M | 25.1 | 40.1 | 40.1 | 40.1 |
| 75M | 25.1 | 40.1 | 40.1 | 40.1 |
| 100M | 25.1 | 40.1 | 40.1 | 40.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.801 | 1.583 | 0.824 | 0.799 |
| 10M | 1.009 | 1.990 | 1.695 | 1.012 |
| 25M | 1.086 | 2.158 | 2.014 | 1.117 |
| 50M | 1.271 | 2.499 | 2.324 | 1.428 |
| 75M | 1.427 | 2.727 | 2.497 | 1.716 |
| 100M | 1.532 | 2.870 | 2.603 | 1.940 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.957 | 0.869 | 0.754 | 0.753 |
| 10M | 0.999 | 0.995 | 0.981 | 0.983 |
| 25M | 1.002 | 1.001 | 0.996 | 0.994 |
| 50M | 1.005 | 1.010 | 1.001 | 1.001 |
| 75M | 1.018 | 1.074 | 1.015 | 1.011 |
| 100M | 1.039 | 1.234 | 1.045 | 1.037 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.896 | 0.903 | 0.900 | 0.900 |
| 10M | 0.988 | 0.992 | 0.989 | 0.989 |
| 25M | 0.994 | 1.029 | 0.995 | 0.994 |
| 50M | 0.998 | 1.210 | 0.999 | 0.999 |
| 75M | 0.998 | 1.377 | 0.999 | 0.999 |
| 100M | 0.999 | 1.498 | 1.000 | 0.999 |

## Extra cost of each layout compared with the flat array

Cell = Δ ns/op (Δ LLC misses/op). This is the price of the row table.

**dense / seq / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -0.2 (-0.016) | -1.0 (-0.129) | -0.7 (-0.114) |
| 10M | +0.0 (+0.004) | -0.0 (+0.001) | -0.0 (+0.001) |
| 25M | +0.0 (+0.004) | -0.0 (+0.003) | -0.0 (+0.003) |
| 50M | +0.0 (+0.006) | +0.0 (+0.004) | +0.1 (+0.005) |
| 75M | +0.0 (+0.006) | +0.0 (+0.004) | +0.1 (+0.005) |
| 100M | +0.0 (+0.006) | +0.0 (+0.005) | +0.0 (+0.004) |

**dense / seq / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | +0.3 (-0.054) | +0.2 (-0.143) | +0.2 (-0.128) |
| 10M | +0.3 (-0.034) | +0.4 (-0.031) | +0.4 (-0.031) |
| 25M | +0.4 (-0.034) | +0.4 (-0.029) | +0.4 (-0.029) |
| 50M | +0.4 (-0.033) | +0.4 (-0.028) | +0.4 (-0.021) |
| 75M | +0.4 (-0.032) | +0.4 (-0.028) | +0.4 (-0.021) |
| 100M | +0.4 (-0.032) | +0.4 (-0.028) | +0.4 (-0.029) |

**dense / seq / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -8.0 (-0.179) | -9.5 (-0.265) | -9.7 (-0.271) |
| 10M | -6.6 (-0.074) | -7.0 (-0.084) | -7.1 (-0.084) |
| 25M | -6.2 (-0.046) | -6.5 (-0.050) | -6.6 (-0.051) |
| 50M | -5.6 (-0.018) | -5.9 (-0.021) | -6.0 (-0.022) |
| 75M | -5.1 (-0.005) | -5.3 (-0.007) | -5.4 (-0.007) |
| 100M | -4.6 (-0.001) | -4.8 (-0.003) | -4.8 (-0.003) |

**dense / seq / update**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -0.1 (-0.055) | -0.8 (-0.179) | -0.9 (-0.187) |
| 10M | +0.1 (-0.018) | +0.1 (-0.021) | +0.1 (-0.019) |
| 25M | +0.2 (-0.016) | +0.2 (-0.018) | +0.2 (-0.018) |
| 50M | +0.2 (-0.017) | +0.2 (-0.017) | +0.2 (-0.014) |
| 75M | +0.2 (-0.017) | +0.2 (-0.018) | +0.2 (-0.014) |
| 100M | +0.2 (-0.017) | +0.1 (-0.017) | +0.2 (-0.017) |

**dense / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -0.2 (-0.044) | +0.0 (-0.008) | -0.1 (-0.014) |
| 10M | +0.4 (+0.013) | +0.0 (-0.003) | -0.0 (-0.001) |
| 25M | +0.7 (+0.009) | +0.2 (-0.001) | +0.0 (-0.001) |
| 50M | +0.8 (+0.014) | +0.2 (+0.002) | +0.1 (+0.000) |
| 75M | +1.7 (+0.075) | +0.3 (+0.004) | +0.1 (-0.001) |
| 100M | +3.6 (+0.231) | +0.4 (+0.010) | +0.2 (+0.002) |

**dense / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | +5.1 (+0.059) | +3.6 (+0.018) | +3.5 (+0.008) |
| 10M | +7.2 (+0.008) | +6.5 (-0.008) | +4.9 (-0.017) |
| 25M | +8.2 (-0.007) | +6.1 (-0.014) | +5.1 (-0.026) |
| 50M | +9.3 (+0.109) | +6.6 (+0.013) | +5.0 (-0.011) |
| 75M | +11.4 (+0.452) | +6.5 (-0.013) | +4.5 (-0.066) |
| 100M | +13.4 (+0.694) | +7.0 (+0.004) | +5.0 (-0.059) |

**dense / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -8.5 (-0.180) | -10.2 (-0.328) | -10.4 (-0.335) |
| 10M | -6.7 (-0.074) | -7.1 (-0.087) | -7.2 (-0.087) |
| 25M | -6.1 (-0.047) | -6.5 (-0.053) | -6.6 (-0.053) |
| 50M | -5.6 (-0.018) | -5.9 (-0.022) | -6.0 (-0.022) |
| 75M | -5.0 (-0.006) | -5.2 (-0.009) | -5.3 (-0.009) |
| 100M | -4.6 (-0.002) | -4.8 (-0.004) | -4.8 (-0.003) |

**dense / rand / update**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -1.1 (-0.088) | -2.5 (-0.203) | -2.5 (-0.204) |
| 10M | +0.3 (-0.004) | -0.1 (-0.018) | -0.1 (-0.016) |
| 25M | +0.5 (-0.001) | +0.1 (-0.006) | +0.0 (-0.008) |
| 50M | +0.7 (+0.005) | +0.2 (-0.004) | +0.1 (-0.004) |
| 75M | +1.6 (+0.056) | +0.3 (-0.003) | +0.0 (-0.007) |
| 100M | +3.5 (+0.195) | +0.5 (+0.006) | +0.2 (-0.002) |
