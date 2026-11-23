# DAT scale experiment 2: Cost of the row table (two-level vs flat)

How much slower is the two-level layout than the flat array when its row table lives in L1 (2 MiB rows), L2 (64 KiB rows) or L3 (4 KiB rows)?

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 20:07:22 |
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
| Repetitions | 2 (each measures one counter group; median time) |
| Operations per get / update measurement | min(keys, 20000000) |
| Memory budget (program + structure) | 9.00 GiB; hard stop at 10 GiB |
| MemAvailable at start | 13.30 GiB |
| Peak process memory measured | 1.50 GiB |
| Clustered run length | 1000 keys |

Patterns: **dense** = keys 0 .. N-1; **clustered** = runs of 1000 consecutive keys at random places; **sparse** = N random keys over the whole range. Orders: **seq** = in key-set order, **rand** = random order. Keys are computed on the fly (a reversible scramble), so no key lists use memory; that computation is included in every number (see "Key generation cost").

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
| O(1): instructions/op flat across key counts (flat) | max/min ≤ 1.10 | ❌ FAIL | 1.19 | dense / rand / insert |
| O(1): instructions/op flat across key counts (rows-4K) | max/min ≤ 1.10 | ❌ FAIL | 1.16 | dense / rand / get_miss |
| O(1): instructions/op flat across key counts (rows-64K) | max/min ≤ 1.10 | ❌ FAIL | 1.16 | dense / rand / get_miss |
| O(1): instructions/op flat across key counts (rows-2M) | max/min ≤ 1.10 | ❌ FAIL | 1.16 | dense / rand / get_miss |
| Page faults during measurements (median) | 0 | ✅ PASS | 0 | - |
| Peak process memory | ≤ 9.00 GiB | ✅ PASS | 1.50 GiB | - |

## Key generation cost (included in every time below)

| pattern | seq ns/key | rand ns/key |
|---|---:|---:|
| dense | 0.6 | 9.3 |

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
| 1M | 8.2 | 8.0 | 7.3 | 7.1 |
| 10M | 8.4 | 8.4 | 8.3 | 8.4 |
| 25M | 8.4 | 8.4 | 8.3 | 8.3 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 28.0 | 42.1 | 42.0 | 42.0 |
| 10M | 28.0 | 42.1 | 42.0 | 42.0 |
| 25M | 28.0 | 42.1 | 42.0 | 42.0 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.002 | 0.003 | 0.002 | 0.002 |
| 10M | 0.002 | 0.003 | 0.002 | 0.002 |
| 25M | 0.002 | 0.003 | 0.002 | 0.002 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.904 | 0.883 | 0.798 | 0.793 |
| 10M | 0.937 | 0.936 | 0.935 | 0.935 |
| 25M | 0.937 | 0.937 | 0.936 | 0.936 |

### dense / seq / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 4.8 | 5.1 | 4.8 | 4.8 |
| 10M | 4.7 | 5.0 | 5.0 | 5.0 |
| 25M | 4.7 | 5.0 | 5.0 | 5.0 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 58.0 | 72.0 | 72.0 | 72.0 |
| 10M | 58.0 | 72.0 | 72.0 | 72.0 |
| 25M | 58.0 | 72.0 | 72.0 | 72.0 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.044 | 0.009 | 0.008 | 0.007 |
| 10M | 0.035 | 0.009 | 0.008 | 0.008 |
| 25M | 0.037 | 0.009 | 0.008 | 0.008 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.865 | 0.817 | 0.725 | 0.721 |
| 10M | 0.919 | 0.896 | 0.895 | 0.893 |
| 25M | 0.921 | 0.906 | 0.898 | 0.898 |

### dense / seq / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 10.7 | 5.8 | 2.1 | 1.8 |
| 10M | 8.3 | 3.9 | 2.8 | 2.6 |
| 25M | 7.7 | 4.5 | 4.2 | 4.1 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 39.1 | 43.1 | 43.1 | 43.1 |
| 10M | 40.2 | 44.1 | 44.1 | 44.1 |
| 25M | 42.4 | 46.2 | 46.2 | 46.2 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.439 | 1.082 | 0.884 | 0.000 |
| 10M | 1.413 | 1.039 | 0.881 | 0.000 |
| 25M | 1.360 | 0.991 | 0.869 | 0.000 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.259 | 0.074 | 0.005 | 0.001 |
| 10M | 0.091 | 0.010 | 0.001 | 0.000 |
| 25M | 0.050 | 0.005 | 0.000 | 0.000 |

### dense / seq / update

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 8.3 | 8.3 | 7.6 | 7.4 |
| 10M | 8.6 | 8.7 | 8.7 | 8.7 |
| 25M | 8.5 | 8.7 | 8.7 | 8.7 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 31.0 | 45.0 | 45.0 | 45.0 |
| 10M | 31.0 | 45.0 | 45.0 | 45.0 |
| 25M | 31.0 | 45.0 | 45.0 | 45.0 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.002 | 0.003 | 0.002 | 0.002 |
| 10M | 0.002 | 0.003 | 0.002 | 0.002 |
| 25M | 0.002 | 0.003 | 0.002 | 0.002 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.909 | 0.851 | 0.727 | 0.719 |
| 10M | 0.917 | 0.896 | 0.895 | 0.896 |
| 25M | 0.917 | 0.902 | 0.897 | 0.897 |

### dense / rand / insert

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 12.1 | 12.2 | 11.9 | 11.8 |
| 10M | 14.5 | 16.1 | 15.1 | 14.9 |
| 25M | 14.2 | 15.9 | 14.9 | 14.5 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 50.8 | 64.9 | 64.8 | 64.8 |
| 10M | 60.3 | 74.4 | 74.3 | 74.3 |
| 25M | 55.3 | 69.4 | 69.3 | 69.3 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.757 | 1.530 | 0.780 | 0.762 |
| 10M | 0.973 | 1.947 | 1.640 | 0.978 |
| 25M | 1.033 | 2.099 | 1.950 | 1.059 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.886 | 0.835 | 0.871 | 0.872 |
| 10M | 0.985 | 0.992 | 0.981 | 0.985 |
| 25M | 0.997 | 1.006 | 0.998 | 0.995 |

### dense / rand / get_hit

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 19.3 | 24.1 | 23.1 | 23.1 |
| 10M | 24.0 | 30.7 | 28.3 | 27.9 |
| 25M | 22.2 | 29.5 | 27.3 | 26.4 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 80.8 | 94.7 | 94.7 | 94.7 |
| 10M | 90.3 | 103.9 | 103.9 | 103.9 |
| 25M | 85.3 | 99.0 | 99.0 | 99.0 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.803 | 2.616 | 1.849 | 1.807 |
| 10M | 1.983 | 2.967 | 2.727 | 2.008 |
| 25M | 2.095 | 3.163 | 3.052 | 2.164 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.569 | 1.616 | 1.578 | 1.583 |
| 10M | 1.960 | 1.964 | 1.964 | 1.963 |
| 25M | 1.981 | 1.981 | 1.985 | 1.980 |

### dense / rand / get_miss

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 15.0 | 9.8 | 4.4 | 3.9 |
| 10M | 15.3 | 12.2 | 11.7 | 11.6 |
| 25M | 12.6 | 10.2 | 9.7 | 9.6 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 61.9 | 65.9 | 65.9 | 65.9 |
| 10M | 72.5 | 76.4 | 76.4 | 76.4 |
| 25M | 69.7 | 73.5 | 73.5 | 73.5 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 1.441 | 1.085 | 0.909 | 0.000 |
| 10M | 1.416 | 1.041 | 0.902 | 0.000 |
| 25M | 1.363 | 0.993 | 0.888 | 0.000 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.335 | 0.153 | 0.008 | 0.001 |
| 10M | 0.098 | 0.014 | 0.001 | 0.000 |
| 25M | 0.053 | 0.006 | 0.000 | 0.000 |

### dense / rand / update

Time per operation (ns/op)

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 13.4 | 12.6 | 11.1 | 11.0 |
| 10M | 14.7 | 16.1 | 15.1 | 14.9 |
| 25M | 14.5 | 16.1 | 15.0 | 14.7 |

Instructions per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 52.8 | 66.8 | 66.8 | 66.8 |
| 10M | 62.3 | 76.3 | 76.3 | 76.3 |
| 25M | 57.3 | 71.3 | 71.3 | 71.3 |

L1D load misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.758 | 1.531 | 0.780 | 0.761 |
| 10M | 0.973 | 1.946 | 1.638 | 0.977 |
| 25M | 1.032 | 2.094 | 1.950 | 1.059 |

LLC misses per operation

| keys | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| 1M | 0.955 | 0.872 | 0.754 | 0.749 |
| 10M | 0.999 | 0.996 | 0.981 | 0.980 |
| 25M | 1.002 | 0.998 | 0.993 | 0.993 |

## Extra cost of each layout compared with the flat array

Cell = Δ ns/op (Δ LLC misses/op). This is the price of the row table.

**dense / seq / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -0.2 (-0.020) | -0.9 (-0.105) | -1.0 (-0.111) |
| 10M | +0.0 (-0.001) | -0.0 (-0.003) | -0.0 (-0.002) |
| 25M | +0.0 (-0.000) | -0.0 (-0.001) | -0.0 (-0.001) |

**dense / seq / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | +0.3 (-0.048) | +0.0 (-0.141) | +0.0 (-0.145) |
| 10M | +0.3 (-0.023) | +0.3 (-0.025) | +0.3 (-0.026) |
| 25M | +0.3 (-0.015) | +0.3 (-0.023) | +0.3 (-0.023) |

**dense / seq / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -4.9 (-0.185) | -8.6 (-0.254) | -8.8 (-0.258) |
| 10M | -4.4 (-0.082) | -5.6 (-0.090) | -5.7 (-0.091) |
| 25M | -3.2 (-0.045) | -3.5 (-0.050) | -3.6 (-0.050) |

**dense / seq / update**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -0.0 (-0.058) | -0.7 (-0.182) | -0.9 (-0.190) |
| 10M | +0.1 (-0.021) | +0.1 (-0.022) | +0.1 (-0.021) |
| 25M | +0.2 (-0.016) | +0.1 (-0.020) | +0.1 (-0.020) |

**dense / rand / insert**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | +0.1 (-0.051) | -0.2 (-0.015) | -0.3 (-0.014) |
| 10M | +1.6 (+0.007) | +0.6 (-0.003) | +0.4 (+0.001) |
| 25M | +1.7 (+0.009) | +0.7 (+0.001) | +0.3 (-0.002) |

**dense / rand / get_hit**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | +4.9 (+0.047) | +3.9 (+0.009) | +3.9 (+0.015) |
| 10M | +6.7 (+0.005) | +4.3 (+0.005) | +3.9 (+0.003) |
| 25M | +7.3 (+0.000) | +5.1 (+0.004) | +4.2 (-0.001) |

**dense / rand / get_miss**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -5.2 (-0.182) | -10.6 (-0.327) | -11.1 (-0.334) |
| 10M | -3.1 (-0.084) | -3.6 (-0.098) | -3.7 (-0.098) |
| 25M | -2.4 (-0.047) | -2.9 (-0.053) | -3.0 (-0.053) |

**dense / rand / update**

| keys | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|
| 1M | -0.8 (-0.083) | -2.3 (-0.201) | -2.4 (-0.206) |
| 10M | +1.4 (-0.003) | +0.4 (-0.018) | +0.2 (-0.018) |
| 25M | +1.6 (-0.004) | +0.5 (-0.009) | +0.2 (-0.009) |
