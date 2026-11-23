# DAT scale experiment 2: Cost of the row table (two-level vs flat)

How much slower is the two-level layout than the flat array when its row table lives in L1 (2 MiB rows), L2 (64 KiB rows) or L3 (4 KiB rows)?

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:27:20 |
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
| Key counts | 1M, 10M, 25M (key range 0 .. 2^27-1 = 134,217,728 keys) |
| Patterns | dense |
| Orders | seq, rand |
| Operations | insert, get_hit, get_miss, update |
| Repetitions | 3 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.35 GiB |
| Peak process memory measured | 1.50 GiB |
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

## Checks

| Check | Target | Result | Worst | Where |
|---|---|---|---|---|
| Correctness (every get returned its own key) | all ok | ✅ PASS | - | - |
| O(1): instructions/op flat across key counts (flat) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / seq / get_miss |
| O(1): instructions/op flat across key counts (rows-4K) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / rand / get_miss |
| O(1): instructions/op flat across key counts (rows-64K) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / seq / get_miss |
| O(1): instructions/op flat across key counts (rows-2M) | max/min ≤ 1.10 | ✅ PASS | 1.00 | dense / rand / get_miss |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 1.50 GiB | - |

## Key generation cost (NOT included in the measurements)

Keys are computed in batches of 4096 between measured sections; the timer and counters see only the structure work. For information, computing a key costs:

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.4 | 9.5 |

## Memory per key

Bytes of memory holding data, per key (ran) or needed (skipped). A 64 B node is the minimum.

| keys | pattern | flat | rows-4K | rows-64K | rows-2M |
|---|---|---:|---:|---:|---:|
| 1M | dense | 64 | 72 | 65 | 65 |
| 10M | dense | 64 | 65 | 64 | 64 |
| 25M | dense | 64 | 64 | 64 | 64 |

## Results

Rows = key count, columns = structure. Medians per operation.

### dense / seq / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 7.5 | 7.4 | 6.6 | 6.2 |
| 10M | 7.9 | 7.9 | 7.9 | 7.9 |
| 25M | 7.9 | 8.0 | 7.9 | 7.9 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 20.1 | 34.2 | 34.1 | 34.1 |
| 10M | 20.1 | 34.2 | 34.1 | 34.1 |
| 25M | 20.1 | 34.2 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.012 | 0.016 | 0.017 | 0.021 |
| 10M | 0.012 | 0.017 | 0.016 | 0.016 |
| 25M | 0.012 | 0.017 | 0.016 | 0.016 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.892 | 0.881 | 0.768 | 0.792 |
| 10M | 0.935 | 0.940 | 0.937 | 0.937 |
| 25M | 0.935 | 0.939 | 0.938 | 0.938 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.015 | 0.016 | 0.015 | 0.015 |
| 10M | 0.016 | 0.016 | 0.016 | 0.016 |
| 25M | 0.016 | 0.016 | 0.016 | 0.016 |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 4.6 | 4.9 | 4.8 | 4.8 |
| 10M | 4.5 | 4.8 | 4.8 | 4.8 |
| 25M | 4.4 | 4.8 | 4.8 | 4.8 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 57.1 | 73.1 | 73.1 | 73.1 |
| 10M | 57.1 | 73.1 | 73.1 | 73.1 |
| 25M | 57.1 | 73.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.082 | 0.022 | 0.020 | 0.019 |
| 10M | 0.069 | 0.022 | 0.021 | 0.021 |
| 25M | 0.069 | 0.022 | 0.021 | 0.022 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.858 | 0.806 | 0.741 | 0.721 |
| 10M | 0.925 | 0.892 | 0.896 | 0.896 |
| 25M | 0.926 | 0.893 | 0.898 | 0.899 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.000 | 0.000 | 0.000 | 0.000 |
| 10M | 0.000 | 0.000 | 0.000 | 0.000 |
| 25M | 0.000 | 0.000 | 0.000 | 0.000 |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 10.5 | 2.6 | 1.1 | 0.9 |
| 10M | 8.1 | 1.4 | 1.0 | 0.9 |
| 25M | 7.5 | 1.3 | 1.0 | 0.8 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 11.1 | 16.1 | 16.1 | 16.1 |
| 10M | 11.1 | 16.1 | 16.1 | 16.1 |
| 25M | 11.1 | 16.1 | 16.1 | 16.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.537 | 1.116 | 0.901 | 0.011 |
| 10M | 1.512 | 1.078 | 0.899 | 0.010 |
| 25M | 1.457 | 1.017 | 0.889 | 0.007 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.266 | 0.096 | 0.008 | 0.001 |
| 10M | 0.085 | 0.011 | 0.001 | 0.000 |
| 25M | 0.052 | 0.005 | 0.000 | 0.000 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.002 | 0.137 | 0.000 | 0.000 |
| 10M | 1.002 | 0.074 | 0.000 | 0.000 |
| 25M | 1.001 | 0.003 | 0.000 | 0.000 |

### dense / seq / update

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 8.0 | 7.9 | 7.1 | 7.1 |
| 10M | 8.2 | 8.3 | 8.3 | 8.3 |
| 25M | 8.2 | 8.3 | 8.3 | 8.3 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 25.1 | 40.1 | 40.1 | 40.1 |
| 10M | 25.1 | 40.1 | 40.1 | 40.1 |
| 25M | 25.1 | 40.1 | 40.1 | 40.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.009 | 0.009 | 0.007 | 0.007 |
| 10M | 0.009 | 0.010 | 0.009 | 0.008 |
| 25M | 0.009 | 0.010 | 0.009 | 0.008 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.906 | 0.851 | 0.722 | 0.718 |
| 10M | 0.917 | 0.898 | 0.897 | 0.898 |
| 25M | 0.917 | 0.900 | 0.900 | 0.900 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.015 | 0.016 | 0.015 | 0.015 |
| 10M | 0.016 | 0.016 | 0.016 | 0.016 |
| 25M | 0.016 | 0.016 | 0.016 | 0.016 |

### dense / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 11.2 | 10.8 | 11.2 | 11.0 |
| 10M | 13.0 | 13.4 | 13.0 | 13.0 |
| 25M | 13.2 | 13.9 | 13.4 | 13.2 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 20.1 | 34.2 | 34.1 | 34.1 |
| 10M | 20.1 | 34.2 | 34.1 | 34.1 |
| 25M | 20.1 | 34.2 | 34.1 | 34.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.802 | 1.589 | 0.837 | 0.812 |
| 10M | 1.017 | 1.999 | 1.704 | 1.024 |
| 25M | 1.089 | 2.170 | 2.022 | 1.128 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.880 | 0.834 | 0.883 | 0.868 |
| 10M | 0.985 | 0.998 | 0.984 | 0.983 |
| 25M | 0.995 | 1.005 | 0.997 | 0.995 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.891 | 0.902 | 0.901 | 0.900 |
| 10M | 0.988 | 0.992 | 0.990 | 0.989 |
| 25M | 0.994 | 1.030 | 0.995 | 0.995 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 12.9 | 17.6 | 16.4 | 16.4 |
| 10M | 13.7 | 21.0 | 20.2 | 18.5 |
| 25M | 13.9 | 22.0 | 20.6 | 19.0 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 57.1 | 73.1 | 73.1 | 73.1 |
| 10M | 57.1 | 73.1 | 73.1 | 73.1 |
| 25M | 57.1 | 73.1 | 73.1 | 73.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.820 | 2.643 | 1.880 | 1.827 |
| 10M | 2.003 | 3.001 | 2.766 | 2.017 |
| 25M | 2.126 | 3.221 | 3.107 | 2.197 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.594 | 1.643 | 1.610 | 1.607 |
| 10M | 2.022 | 2.026 | 2.007 | 1.996 |
| 25M | 2.047 | 2.052 | 2.042 | 2.028 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.891 | 0.901 | 0.900 | 0.899 |
| 10M | 0.988 | 0.991 | 0.990 | 0.989 |
| 25M | 0.994 | 1.029 | 0.995 | 0.994 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 11.3 | 2.8 | 1.1 | 0.9 |
| 10M | 8.0 | 1.4 | 1.0 | 0.9 |
| 25M | 7.4 | 1.3 | 1.0 | 0.8 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 11.1 | 16.1 | 16.1 | 16.1 |
| 10M | 11.1 | 16.1 | 16.1 | 16.1 |
| 25M | 11.1 | 16.1 | 16.1 | 16.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.538 | 1.119 | 0.925 | 0.012 |
| 10M | 1.505 | 1.080 | 0.918 | 0.012 |
| 25M | 1.448 | 1.020 | 0.907 | 0.008 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.336 | 0.157 | 0.010 | 0.001 |
| 10M | 0.091 | 0.014 | 0.001 | 0.001 |
| 25M | 0.052 | 0.006 | 0.000 | 0.000 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.002 | 0.137 | 0.000 | 0.000 |
| 10M | 1.000 | 0.075 | 0.000 | 0.000 |
| 25M | 1.000 | 0.004 | 0.000 | 0.000 |

### dense / rand / update

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 12.6 | 11.5 | 10.1 | 10.1 |
| 10M | 13.3 | 13.6 | 13.2 | 13.2 |
| 25M | 13.5 | 14.0 | 13.6 | 13.5 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 25.1 | 40.1 | 40.1 | 40.1 |
| 10M | 25.1 | 40.1 | 40.1 | 40.1 |
| 25M | 25.1 | 40.1 | 40.1 | 40.1 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.797 | 1.581 | 0.823 | 0.799 |
| 10M | 1.009 | 1.992 | 1.698 | 1.015 |
| 25M | 1.084 | 2.160 | 2.016 | 1.121 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.960 | 0.870 | 0.747 | 0.749 |
| 10M | 1.000 | 0.995 | 0.983 | 0.984 |
| 25M | 1.001 | 1.000 | 0.995 | 0.995 |

dTLB misses per operation (load + store)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.892 | 0.903 | 0.900 | 0.900 |
| 10M | 0.988 | 0.992 | 0.990 | 0.989 |
| 25M | 0.994 | 1.030 | 0.995 | 0.995 |

## Extra cost of each layout compared with the flat array

Cell = Δ ns/op (Δ LLC misses/op). This is the price of the row table.

**dense / seq / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -0.1 (-0.011) | -0.9 (-0.124) | -1.3 (-0.101) |
| 10M | +0.0 (+0.005) | -0.0 (+0.002) | -0.0 (+0.002) |
| 25M | +0.1 (+0.005) | +0.0 (+0.003) | +0.0 (+0.003) |

**dense / seq / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | +0.3 (-0.052) | +0.1 (-0.117) | +0.1 (-0.137) |
| 10M | +0.4 (-0.033) | +0.4 (-0.030) | +0.4 (-0.030) |
| 25M | +0.3 (-0.033) | +0.4 (-0.028) | +0.4 (-0.027) |

**dense / seq / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -8.0 (-0.170) | -9.4 (-0.258) | -9.7 (-0.265) |
| 10M | -6.7 (-0.074) | -7.1 (-0.084) | -7.2 (-0.085) |
| 25M | -6.2 (-0.047) | -6.5 (-0.051) | -6.7 (-0.052) |

**dense / seq / update**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -0.1 (-0.055) | -0.8 (-0.184) | -0.8 (-0.188) |
| 10M | +0.2 (-0.018) | +0.1 (-0.020) | +0.1 (-0.019) |
| 25M | +0.2 (-0.017) | +0.2 (-0.017) | +0.2 (-0.017) |

**dense / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -0.4 (-0.046) | +0.0 (+0.003) | -0.2 (-0.012) |
| 10M | +0.5 (+0.012) | +0.1 (-0.001) | +0.0 (-0.003) |
| 25M | +0.6 (+0.011) | +0.2 (+0.002) | +0.0 (+0.000) |

**dense / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | +4.6 (+0.049) | +3.5 (+0.016) | +3.5 (+0.013) |
| 10M | +7.3 (+0.004) | +6.5 (-0.014) | +4.8 (-0.026) |
| 25M | +8.1 (+0.005) | +6.7 (-0.004) | +5.1 (-0.018) |

**dense / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -8.5 (-0.179) | -10.2 (-0.327) | -10.5 (-0.336) |
| 10M | -6.6 (-0.077) | -7.1 (-0.090) | -7.2 (-0.090) |
| 25M | -6.1 (-0.045) | -6.4 (-0.051) | -6.5 (-0.052) |

**dense / rand / update**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -1.0 (-0.089) | -2.5 (-0.213) | -2.5 (-0.211) |
| 10M | +0.3 (-0.005) | -0.1 (-0.017) | -0.1 (-0.015) |
| 25M | +0.5 (-0.001) | +0.1 (-0.006) | +0.0 (-0.006) |
