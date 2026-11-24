# Experiment 6: threads at every key count

## System

| Item | Value |
|---|---|
| Date | 2026-11-23 21:35:48 |
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
| Layouts | flat, rows-2M |
| Lock designs | mutex, spin, nodelock, seqlock |
| Key counts | 1M, 10M, 25M, 50M, 75M, 100M (dense keys 0 .. N-1) |
| Threads | 1, 2, 4, 6, 8, 12 |
| Workloads | load = insert every key into an empty table; A = 50% get / 50% update; C = 100% get; W = 100% update |
| Key distributions | uniform, zipf (zipf: theta 0.99, hottest keys 0, 1, 2, ...) |
| Operations per thread per workload | 500000 |
| Repetitions | 3 (median throughput; latency percentiles over all samples, 1 in 64 ops timed) |
| Pages | hugetlb |
| Memory budget | 9.00 GiB; hard stop 10 GiB; peak measured 0.00 GiB |
| CPU pinning | --cpus list |

## Memory plan

| keys | layout | memory (structure + load keys + operation lists + program) | decision |
|---|---|---:|---|
| 1M | flat | 0.49 GiB | ⏭ skipped: needs 31 huge pages, 0 free |
| 1M | rows-2M | 0.49 GiB | ⏭ skipped: needs 32 huge pages, 0 free |
| 10M | flat | 1.06 GiB | ⏭ skipped: needs 306 huge pages, 0 free |
| 10M | rows-2M | 1.06 GiB | ⏭ skipped: needs 307 huge pages, 0 free |
| 25M | flat | 2.00 GiB | ⏭ skipped: needs 763 huge pages, 0 free |
| 25M | rows-2M | 2.01 GiB | ⏭ skipped: needs 764 huge pages, 0 free |
| 50M | flat | 3.59 GiB | ⏭ skipped: needs 1526 huge pages, 0 free |
| 50M | rows-2M | 3.59 GiB | ⏭ skipped: needs 1527 huge pages, 0 free |
| 75M | flat | 5.17 GiB | ⏭ skipped: needs 2289 huge pages, 0 free |
| 75M | rows-2M | 5.17 GiB | ⏭ skipped: needs 2290 huge pages, 0 free |
| 100M | flat | 6.76 GiB | ⏭ skipped: needs 3052 huge pages, 0 free |
| 100M | rows-2M | 6.76 GiB | ⏭ skipped: needs 3053 huge pages, 0 free |
