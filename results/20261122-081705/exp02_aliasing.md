# Experiment 02: cache-set aliasing per j (analytic)

Cache geometry: this machine (sysfs). Window = W consecutive keys (a dense, sequential working set).

## L1D: 64 sets x 12 ways, 64 B lines (exact with any page size)

Cell = most keys in one set out of W consecutive keys starting at 0. ✅ ≤ 12 ways, ❌ conflict misses.

| j | W = 64 | W = 512 | W = 1024 | W = 4096 | W = 16384 | W = 65536 |
|---|---:|---:|---:|---:|---:|---:|
| ideal | 1 | 8 | 16 | 64 | 256 | 1024 |
| 0 | 64 ❌ | 512 ❌ | 1024 ❌ | 4096 ❌ | 16384 ❌ | 16384 ❌ |
| 6 | 1 ✅ | 8 ✅ | 16 ❌ | 64 ❌ | 256 ❌ | 1024 ❌ |
| 8 | 1 ✅ | 8 ✅ | 16 ❌ | 64 ❌ | 256 ❌ | 1024 ❌ |
| 11 | 1 ✅ | 8 ✅ | 16 ❌ | 64 ❌ | 256 ❌ | 1024 ❌ |
| 15 | 1 ✅ | 8 ✅ | 16 ❌ | 64 ❌ | 256 ❌ | 1024 ❌ |
| 20 | 1 ✅ | 8 ✅ | 16 ❌ | 64 ❌ | 256 ❌ | 1024 ❌ |

## L2: 2048 sets x 10 ways, 64 B lines (exact with 2 MiB pages; partly randomised by 4 KiB pages)

Cell = most keys in one set out of W consecutive keys starting at 0. ✅ ≤ 10 ways, ❌ conflict misses.

| j | W = 64 | W = 512 | W = 1024 | W = 4096 | W = 16384 | W = 65536 |
|---|---:|---:|---:|---:|---:|---:|
| ideal | 1 | 1 | 1 | 2 | 8 | 32 |
| 0 | 64 ❌ | 512 ❌ | 512 ❌ | 512 ❌ | 512 ❌ | 512 ❌ |
| 6 | 1 ✅ | 8 ✅ | 16 ❌ | 64 ❌ | 256 ❌ | 512 ❌ |
| 8 | 1 ✅ | 2 ✅ | 4 ✅ | 16 ❌ | 64 ❌ | 256 ❌ |
| 11 | 1 ✅ | 1 ✅ | 1 ✅ | 2 ✅ | 8 ✅ | 32 ❌ |
| 15 | 1 ✅ | 1 ✅ | 1 ✅ | 2 ✅ | 8 ✅ | 32 ❌ |
| 20 | 1 ✅ | 1 ✅ | 1 ✅ | 2 ✅ | 8 ✅ | 32 ❌ |
