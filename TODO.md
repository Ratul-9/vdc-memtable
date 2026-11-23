# TODO

Legend: [x] done | [ ] to do | (P#) = problem number in `memory and cache related problems.md` | (T#) = section in `THINGS_TO_THINK_ABOUT.md`

## Done

### Research and design docs
- [x] DESIGN.md: VDC-Space idea, revised direct-addressed model (slot = bit reversal of key), 2^20 slots, size table, engineering notes
- [x] THINGS_TO_THINK_ABOUT.md: 13 sections of open problems
- [x] memory and cache related problems.md: 30 problems with status (solved / open / deferred), plus a review of problems 1-19
- [x] Prior-art check: no direct match found for VDC slot placement (related: RocksDB hash memtables, Speedb, FloDB, split-ordered lists, Fibonacci hashing)

### Decisions made
- [x] Language: C
- [x] Key domain: integers 0 .. 2^20 - 1, direct addressed, no hashing, no probing
- [x] Partial (m-bit) reversal chosen over full reversal as the default; j (unreversed low bits) is tunable
- [x] Slot size: 64 B (one Node = one cache line)
- [x] Node layout: 3 B key, 5 B reserved (empty), 8 B seqno, 8 B ext_off, 4 B val_len, 4 B flags, 32 B inline value
- [x] Compaction is out of scope for now; concurrency and NUMA/latency are deferred
- [x] Dev machine is Apple M5: 16 KiB pages, no 2 MiB huge pages, 128 B cache lines. Huge pages / TLB work must be judged on Linux

### Code
- [x] Project skeleton (directories and empty files for src, include, tests, baselines, bench, scripts, 7 experiments)
- [x] compile_flags.txt (editor include path)
- [x] include/vdc/vdc.h: `vdc_slot_full`, `vdc_slot_partial`, inverse functions, bit-reversal helpers (checked exhaustively for j = 0..20 in a scratch test)
- [x] include/vdc/slot.h: `Node` (64 B, 64 B aligned, offset static asserts), `node_set_key` / `node_get_key`
- [x] include/vdc/vdc_space.h + src/vdc_space.c: `VdcSpace` = 2^20 Nodes (64 MiB), init/destroy via mmap, `vdc_space_node(space, key)` (checked by a scratch round-trip test)
- [x] include/vdc/arena.h + src/arena.c: bump-pointer arena returning offsets (checked by a scratch test)
- [x] include/vdc/memtable.h + src/memtable.c: `Memtable` = VdcSpace + Arena; put / get / delete (tombstone), stats (count, tombstones, entries, capacity, is_empty/is_full, memory/node/arena/value bytes, seqno); inline values <= 32 B, larger values in the arena with in-place reuse (checked with a scratch test incl. 2M random ops against a model)
- [x] tests/: tests.h (CHECK macro), test_main.c, test_vdc_bijection, test_partial_reversal, test_node, test_vdc_space, test_arena, test_memtable (moved from scratch tests); 6/6 pass
- [x] Makefile: `make` (build/libvdc.a), `make test`, `make debug` (-O0, VDC_DEBUG, ASan+UBSan), `make clean`; .gitignore for build dirs
- [x] Memtable operations: put / get / delete (tombstone) / stats are in memtable.h (done)

### Single-threaded completion + Linux test suite (built and compiled on macOS, not run yet)
- [x] Decisions: key counts 1K/10K/100K/500K/1M (2M skipped), j = 6/8/11/15/20, 32 B inline values only, page modes base + hugetlb, bitmap built but not wired into the memtable
- [x] include/vdc/config.h: VDC_DEFAULT_J (6), VDC_PREFETCH_DISTANCE (16), VDC_HUGE_PAGE_SIZE
- [x] Platform layer (platform.h, platform_linux.c, platform_posix.c): runtime page size, base pages with THP disabled per mapping, reserved 2 MiB hugetlb pool (MAP_HUGETLB | MAP_HUGE_2MB), populate (MAP_POPULATE / touch), prefetch helpers (P14, P15, P10)
- [x] VdcSpace / Arena / Memtable take a page mode + populate (`vdc_space_init_ex`, `arena_init_ex`, `MemtableConfig` + `memtable_init_config`); `memtable_clear` / `vdc_space_clear` for reuse
- [x] Batched API with rolling software prefetch: `memtable_get_batch`, `memtable_put_batch` (P17)
- [x] Occupancy bitmap (bitmap.h / bitmap.c): 1 bit per slot + summary level, `bitmap_next_set` (P19), standalone
- [x] Unit tests: test_bitmap, test_platform, batch / clear / config tests in test_memtable (8 test groups)
- [x] Baselines: linear-probing hash table, skip list (same semantics, same page modes); identity = vdc with j = 20
- [x] vdc_bench: phases insert / get_hit / get_miss / get_batch / update / delete; patterns seq / dense_rand / rand; perf counters (cycles, instructions, L1D, LLC, dTLB load/store, page faults) in groups; markdown report (Checks, O(1) detail, huge pages vs base, fastest variant, per-workload pivot tables) + CSV
- [x] Experiments: 01 bijection + pages touched, 02 cache-set aliasing per j (analytic), 04 prefetch distance sweep, 05 layout report, 06 create / fill / clear cost; 03 and 07 run through vdc_bench (READMEs)
- [x] Scripts: setup_hugepages.sh, perf_counters.sh, run_bench.sh (one command for everything), plot_results.py (pivot tables from CSV)
- [x] Makefile: `make` builds lib, tests, vdc_bench, experiments; `make bench`, `make experiments`; README.md with the Linux run guide

### Multithreaded designs (built and tested on macOS, not run on Linux yet)
- [x] Node layout: the 5 reserved bytes are now `lock` (1 B, node-lock mode) and `version` (4 B, seqlock); still 64 B
- [x] Direct thread-safe memtables (concurrent.h): mutex, striped spinlocks, striped RW locks, per-node lock, seqlock (lock-free reads)
- [x] Lock-free bounded queues (queue.h): Vyukov MPMC ring + MPSC pop, 64 B cells
- [x] Delegation service (service.h): MPSC (1 owner), sharded MPSC queues per key-range owner, shared MPMC queue with forwarding; sync and async producers
- [x] Unit tests: queue exactly-once with 4+4 threads, no torn reads under hot-key contention for all 5 locking modes, service results / ownership / per-owner order; passes under ThreadSanitizer (`make tsan`)
- [x] vdc_mtbench + scripts/run_mt_bench.sh + CONCURRENCY.md; smoke run on macOS: all 11 designs correct (0 torn reads, 0 wrong statuses)

## To do

### Next: run on Linux
- [ ] Run `scripts/run_bench.sh` on bare-metal Linux (setup: `sudo scripts/setup_hugepages.sh`, `scripts/perf_counters.sh`)
- [ ] Review the Checks table: O(1) (instructions/op), LLC misses/op, dTLB misses/op on hugetlb, page faults, correctness
- [ ] Choose j from the j sweep and set VDC_DEFAULT_J; choose VDC_PREFETCH_DISTANCE from experiment 04
- [ ] Linux-only code (perf counters, hugetlb) has only been reviewed, not compiled: fix any build issue on first run

### Code: core
- [ ] Decide the real arena capacity (currently a 16 MiB placeholder, `MEMTABLE_DEFAULT_ARENA_BYTES`); garbage left by shrinking/replaced external values is only reclaimed on reset (P8)
- [ ] Version policy: memtable keeps newest only; decide if snapshots are ever needed (P7, T7)
- [ ] Decide whether the stored key (3 B) stays, given slot -> key is invertible
- [ ] Decide what to use the 5 reserved bytes for (or leave empty)
- [ ] Wire the bitmap into the memtable when flush / ordered iteration is built (P19)
- [ ] Ordered iteration / flush: walk keys in order using runs of 2^j slots, skip empties with the bitmap (P11, P18, P19, T5)

### Space lifecycle
- [ ] Fill-based rotation: when a Space is sealed and a new one takes over; flush triggers beyond "full" (hot-key workloads never fill a Space) (P1, P2)
- [ ] Pre-allocated pool of Spaces (reuse with memtable_clear), pre-fault and zeroing off the hot path, 2x peak memory budget (P9, P10)
- [ ] Router for keys beyond one 2^20 window, and the key -> dense id mapping (a dictionary is itself a lookup structure) (P3, T4, T13)
- [ ] Decide behaviour for keys outside the dense window and for skewed windows (P3)

### Experiments (ready, not run)
- [ ] Run 01-07 on Linux (all included in scripts/run_bench.sh) and record conclusions
- [ ] Range scan / flush benchmark (bench/bench_scan_flush.c placeholder) once ordered iteration exists
- [ ] 2M keys: needs rotation or multi-Space routing (skipped for now)

### Open problems still marked OPEN or "assumption" in the memory doc
- [ ] P3 many Spaces: replace the "assumption" with a real design (router + id mapping)
- [ ] P4 page commit under scatter: moot with MAP_POPULATE + huge pages, confirm on Linux
- [ ] P11, P16, P18: partial reversal is the fix; confirm with experiment 02
- [ ] P14, P15: hugetlb plan; confirm with experiment 03
- [ ] P17: software prefetch; only helps batched operations, confirm with experiment 04
- [ ] P5-P10, P19: "proposed, to confirm" items from the review; turn each into a decision

### Deferred (later)
- [ ] Run vdc_mtbench on Linux and analyse: scaling, hot keys (P22), shared counters avoided by design (P23), atomics cost (P24), torn reads checked (P25), VDC scatter vs identity
- [ ] `perf c2c` run for cache-line contention (HITM) on the best and worst designs
- [ ] Bitmap contention (P20) once the bitmap is wired in
- [ ] NUMA, memory bandwidth, first-touch page faults, swap/memory pressure, multi-Space interference (P26-P30)
- [ ] Compaction strategy
- [ ] Keys wider than 20 bits (T12)
- [ ] Persistence / WAL
