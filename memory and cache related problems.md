# Memory and Cache Related Problems

Applies to the direct-addressed model: one VDC-Space of 2^20 slots, slot = bit-reversal (full or partial) of the key.

Legend: ✅ solved / minor (easy fix) | 🔴 OPEN (needs a solution) | ⏳ DEFERRED (concurrency, NUMA, latency: later)

Decisions so far: slot size = 64 B (one cache line, so 2^20 x 64 B = 64 MiB; problem 12 solved).
The full 64 MiB does not need to fit in cache (13). High fill rate means upfront allocation is not waste (1, 2).

**Decision: partial (m-bit) reversal, not full 20-bit reversal.**
With j = 20 - m unreversed low key bits:

    slot = (rev_m(key >> j) << j) | (key & (2^j - 1))

- Keys that differ only in the low j bits sit in 2^j consecutive 64 B slots,
  i.e. a contiguous run of 64 * 2^j bytes. j = 6 gives a 4 KiB run (one page).
- Ordered scan/flush reads runs of 2^j slots sequentially, so the prefetcher
  works and cache misses drop by about 2^j (fixes 11, 18).
- Consecutive keys now use different L1/L2 set-index bits (slot low bits come
  from the unreversed key bits), removing the set-conflict pile-up (fixes 16).
- Fewer pages touched per key range (helps 4, still open for sparse Spaces).
- Prefetch (problem 17): compute slot addresses for a batch of keys first, issue
  `__builtin_prefetch` for each, then process, so misses overlap. Addresses
  depend only on the key, no dependent loads. Adjacent-line prefetcher waste is
  an accepted fixed cost, to be measured.
- m is a tunable: m = 20 full scatter, m = 0 identity. Pick j by benchmark;
  start with j = 6 (page-sized runs).

## Review of problems 1-19 (status check)

All 19 are ticked, but not equally solid.

### Fixed with a concrete mechanism: 11, 12, 13, 14, 15, 16, 17, 18
- 11 (spatial locality), 16 (set conflicts), 18 (ordered scan): partial reversal.
  Consecutive keys sit in adjacent slots; flush reads 4 KiB runs sequentially.
- 12 (line straddling), 13 (working set vs cache): 64 B slot decision.
- 14, 15 (TLB, huge pages): reserved hugetlb pool + `MAP_POPULATE`. Linux-only;
  does not work on the macOS dev machine.
- 17 (prefetcher): software prefetch. Only helps batched operations, not
  single-key lookups.

### Ticked because of an assumption: 1, 2, 3, 4
- 1, 2: hold only if a Space is rotated when full. Updates reuse slots, so a
  hot-key workload that overwrites the same keys never fills a Space and never
  rotates it. The LSM also needs flush triggers for WAL size and time, so flush
  policy must be fill-based plus something else.
- 3: "collision-free key-to-id mapping + spread writes evenly" restates the
  dense-key assumption, it is not a solution. A dictionary is itself a lookup
  structure, and skewed writes break the even-spread assumption.
- 4: moot rather than solved. With `MAP_POPULATE` and huge pages the full
  64 MiB is committed on creation anyway.

### Ticked as "proposed, to confirm": 5, 6, 7, 8, 9, 10, 19
- Reasonable ideas, not yet decisions.
- 7: "one version per slot" removes snapshot reads. It is a design choice, not
  a fix.
- 5: with 64 B slots the inline value gets roughly 48-56 B after the header.

### Check on problem 16 (L2 aliasing)
- With j = 6, L1 conflicts are gone. But the L2 set index uses slot bits up to
  about bit 10, and slot bits 6-10 still come from the reversed high key bits.
  A window of more than about 1K consecutive keys can thrash a subset of L2
  sets.
- Choosing j around 11 covers the L2 set bits, at the cost of less scatter.
  Benchmark j before settling on 6.
- Huge pages do not help: physical and virtual low bits match inside a 2 MiB
  page.

### Suggested status changes
- Mark 1, 2, 3 as "assumption" and note the flush-policy dependency.

## Memory footprint

1. ✅ **Fixed allocation.** All 2^20 slots (24 MiB at 24 B, 32 MiB at 32 B, 64 MiB at 64 B) are reserved up front, even if few keys exist.
   - **Solution:** Not a problem at high fill rate: the memtable fills to its limit before the next one starts, so all slots are used.
2. ✅ **Low fill rate wastes memory.** Efficiency = present keys / 2^20. A sparse key window holds full memory for little data.
   - **Solution:** Same as 1: the memtable is only rotated once its slots are full, so efficiency stays high.
3. ✅ **Many Spaces multiply the cost.** If keys span several 2^20 windows, each window needs its own Space, and each is mostly empty until it fills.
   - **Solution:** Keep keys in range with a collision-free key-to-id mapping (dictionary/counter, not a hash) and spread writes evenly so every Space fills before rotation.
4. ✅ (partial reversal, residual: sparse keys) **Page commit under scatter.** Full reversal spreads consecutive keys across pages, so a few keys touch many 4 KiB pages. Resident memory (RSS) grows far beyond the data size on sparse workloads.
5. ✅ **Slot size limits values.** 24-64 B slots only hold small values inline. Larger values need an out-of-line arena, which adds a second allocation, pointer, and cache miss.
   - **Solution:** (proposed, to confirm) Keep values small inline in the 64 B slot; put larger values in an out-of-line arena and store its offset in the slot.
6. ✅ **Per-slot metadata overhead.** Occupancy flag, tombstone flag, sequence number/version take bytes from the slot. Occupancy bitmap is 128 KiB per Space.
   - **Solution:** (proposed, to confirm) Pack flags and sequence number into a fixed header inside the 64 B slot; keep the 128 KiB occupancy bitmap per Space.
7. ✅ **Version chains and snapshots.** Keeping older versions needs extra memory beyond one slot per key.
   - **Solution:** (proposed, to confirm) Keep one version per slot (newest), or reserve extra memory only if snapshots are needed.
8. ✅ **Arena fragmentation and growth** for variable-length keys/values stored outside the slot.
   - **Solution:** (proposed, to confirm) Use a bump-pointer arena per Space, freed all at once when the Space is flushed, so no fragmentation.
9. ✅ **Memory reclamation at flush.** A sealed 64 MiB Space must stay alive until flush completes; a new Space is allocated meanwhile (up to 2x peak memory).
   - **Solution:** (proposed, to confirm) Pre-allocate a small pool of Spaces so the next one is ready while the sealed one flushes; accept up to 2x peak memory.
10. ✅ **Zeroing / initialization cost.** Clearing or faulting in 24-64 MiB for each new Space causes a latency spike on rotation.
   - **Solution:** (proposed, to confirm) Pre-fault and zero new Spaces in the background before rotation.

## Cache and TLB behavior

11. ✅ (partial reversal) **Lost spatial locality.** Full bit reversal places sequential keys 2^19, 2^18, ... slots apart, so sequential access gets no cache-line reuse and no hardware prefetch benefit.
12. ✅ **Cache-line straddling.** Slot sizes that do not divide 64 B (such as 24 B) cross two lines, doubling misses for those slots.
   - **Solution:** Slot size is 64 B, exactly one cache line, so no slot straddles two lines; align the base to 64 B.
13. ✅ **Working set exceeds cache.** 24-64 MiB is larger than L1/L2 and often larger than L3 share per core; random access means mostly L3/DRAM misses.
   - **Solution:** The full 64 MiB does not need to fit in cache; each operation touches one 64 B line.
14. ✅ **TLB misses.** Scattered access over 64 MiB with 4 KiB pages needs 16,384 pages, far more than L1/L2 TLB entries.
    **Solution:** back each Space with 2 MiB pages from a reserved hugetlb pool (`mmap` with `MAP_HUGETLB | MAP_HUGE_2MB`, pool sized via `vm.nr_hugepages`). 64 MiB = 32 pages, which fit in the 2 MiB-page TLB, so page walks effectively disappear.
15. ✅ **Huge-page availability.** Transparent huge pages may not be granted, may fragment, or add compaction stalls; explicit hugetlb needs configuration.
    **Solution:** use the reserved pool instead of THP (pages are reserved at boot/config time, so no fragmentation or compaction stalls). Pool size = (number of Spaces incl. sealed ones awaiting flush) x 32 pages. Check `MAP_FAILED` and fail loudly. Pre-fault at Space creation (`MAP_POPULATE`).
    **Remaining caveat:** huge pages fix TLB misses only, not cache misses or set aliasing (problem 16). Many Spaces can exceed 2 MiB-page TLB capacity again (problem 3, scaling).
16. ✅ (partial reversal) **Cache associativity conflicts.** Bit-reversed strides are powers of two; keys differing in high bits map to addresses with the same low set-index bits, causing set conflict misses (cache aliasing).
17. ✅ (software prefetch) **Hardware prefetcher confusion.** Power-of-two strided patterns can mislead or defeat stream/stride prefetchers.
18. ✅ (partial reversal) **Ordered scan is random access.** Iterating keys in order reads slots in bit-reversed order, so every step is a likely cache miss. Flush of 2^20 slots does 2^20 random accesses.
19. ✅ **Empty-slot scanning.** Scans over sparse Spaces read many empty slots unless the occupancy bitmap is used; the bitmap itself must stay hot in cache.
   - **Solution:** (proposed, to confirm) Use the occupancy bitmap (hierarchical if needed) to skip empty ranges.
20. ⏳ DEFERRED **Bitmap contention.** A shared occupancy bitmap makes many writers hit the same cache lines (see false sharing).

## Concurrency effects on cache

21. ⏳ DEFERRED **False sharing.** Slots smaller than a cache line mean writers on different keys can share one line and bounce it between cores.
22. ⏳ DEFERRED **True sharing / hot keys.** Updates to one hot key ping-pong its cache line across cores regardless of layout.
23. ⏳ DEFERRED **Cache-line ping-pong on metadata.** Shared counters (entry count, bytes used, sequence number) become contention points.
24. ⏳ DEFERRED **Atomic update cost.** CAS or atomic publish per slot adds coherence traffic and may need 64 B alignment for lock-free updates.
25. ⏳ DEFERRED **Torn reads.** Slots larger than the atomic width can be read half-written unless a version/lock word protects them.

## NUMA and system effects

26. ⏳ DEFERRED **NUMA placement.** Pages are placed by first touch; scattered first writes from different threads spread a Space across nodes, so many accesses are remote.
27. ⏳ DEFERRED **Memory bandwidth.** Random access with one cache line per operation at high throughput can saturate DRAM bandwidth/latency, not CPU.
28. ⏳ DEFERRED **Page faults on first touch.** Lazily committed pages cause faults during writes, hurting tail latency. Pre-faulting costs time and memory.
29. ⏳ DEFERRED **Swap / memory pressure.** Under overcommit, a large mostly-cold Space can be paged out or hit cgroup limits.
30. ⏳ DEFERRED **Interference with other memtables.** Multiple Spaces in one process compete for L3 and TLB capacity.

## Design knobs that trade these off

- Slot size as a power of two dividing 64 B (32 B or 64 B).
- Partial reversal: keep the low j bits (slots per line) unchanged, reverse only the upper bits, to keep intra-line locality.
- 2 MiB-aligned base and huge pages to remove most TLB cost.
- Padding/alignment of hot metadata and per-core counters to avoid false sharing.
- Hierarchical occupancy bitmap for sparse scans.
- Pre-allocated pool of Spaces to avoid allocation and page-fault spikes on rotation.
