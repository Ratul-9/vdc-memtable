# Experiment 01: bijection check and page footprint

Key domain 0 .. 2^20-1, 64 B Nodes, Space = 64 MiB. j = unreversed low key bits (0 = full reversal, 20 = identity).

## Bijection

| j | bijective | involution | consecutive keys per run | run size |
|---|---|---|---:|---:|
| 0 | ✅ yes | ✅ yes | 1 | 64 B |
| 6 | ✅ yes | ✅ yes | 64 | 4096 B |
| 8 | ✅ yes | ✅ yes | 256 | 16384 B |
| 11 | ✅ yes | ✅ yes | 2048 | 131072 B |
| 15 | ✅ yes | ✅ yes | 32768 | 2097152 B |
| 20 | ✅ yes | ✅ yes | 1048576 | 67108864 B |

## 4 KiB pages touched by keys 0..N-1

Fewer pages = fewer TLB entries for a dense key range. Minimum = N x 64 B / page size.

| j | N = 1000 | N = 10000 | N = 100000 | N = 1000000 |
|---|---:|---:|---:|---:|
| minimum | 16 | 157 | 1563 | 15625 |
| 0 | 1000 | 10000 | 16384 | 16384 |
| 6 | 16 | 157 | 1563 | 15625 |
| 8 | 16 | 157 | 1563 | 15625 |
| 11 | 16 | 157 | 1563 | 15625 |
| 15 | 16 | 157 | 1563 | 15625 |
| 20 | 16 | 157 | 1563 | 15625 |

## 2 MiB pages touched by keys 0..N-1

Fewer pages = fewer TLB entries for a dense key range. Minimum = N x 64 B / page size.

| j | N = 1000 | N = 10000 | N = 100000 | N = 1000000 |
|---|---:|---:|---:|---:|
| minimum | 1 | 1 | 4 | 31 |
| 0 | 32 | 32 | 32 | 32 |
| 6 | 16 | 32 | 32 | 32 |
| 8 | 4 | 32 | 32 | 32 |
| 11 | 1 | 5 | 32 | 32 |
| 15 | 1 | 1 | 4 | 31 |
| 20 | 1 | 1 | 4 | 31 |
