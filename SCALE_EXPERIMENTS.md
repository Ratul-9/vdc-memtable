# Scale experiments (1M – 100M keys) under a 9 GiB memory budget

The experiments that test the direct-address memtable from 1M to 100M keys, including the new two-level "matrix"
layout with rows created on demand. Built for a 16 GB machine where `free -g` shows about 12 GB free.

## Where things are

| Path | What it is |
|---|---|
| `include/dat/dat_node.h` | 64 B slot with a full 4-byte key (keys up to 2^27 = 134,217,728) |
| `include/dat/dat_flat.h`, `src/dat/dat_flat.c` | flat array: slot = key |
| `include/dat/dat_rows.h`, `src/dat/dat_rows.c` | two-level layout: row table + rows created on demand |
| `include/dat/dat_keys.h`, `src/dat/dat_keys.c` | key sets computed on the fly (no key lists in memory) |
| `bench_scale/` | the benchmark `build/dat_scale`: memory planner, memory guard, experiments, report |
| `scripts/scale/run_scale.sh` | runs everything in a safe order |
| `scripts/scale/summarize_scale.py` | combines the results into `summary.md` |
| `tests/test_dat.c` | unit tests (part of `make test`) |

The existing 1M-key code (`include/vdc/`) is unchanged.

## How to run (Linux)

```bash
scripts/scale/run_scale.sh --quick
```

```bash
scripts/scale/run_scale.sh
```

- Run `--quick` first (smaller sizes), then the full run.
- The script asks for `sudo` twice: once to release any reserved huge pages before the normal-page experiments, and
  once to reserve and later give back the huge-page pool for experiment 4.
- Results go to `results/scale-<timestamp>/`. Start with `summary.md`.

Useful options:
- `--budget-gib 9` sets the limit (10 is the absolute maximum).
- `--max-keys 50M` drops bigger sizes.
- `--phase base` or `--phase huge` runs only one phase.

To see what would run and how much memory each piece needs, without allocating anything:

```bash
./build/dat_scale --experiment 3 --plan
```

## The five experiments

| # | Name | Question | Structures | Keys | Patterns | Orders |
|---|---|---|---|---|---|---|
| 1 | flat_scale | How does the flat array behave from 1M to 100M keys? (the reference) | flat | 1M, 10M, 25M, 50M, 75M, 100M | dense | seq, rand |
| 2 | row_cost | What does the row table cost when it lives in L1, L2 or L3? | flat, rows-4K, rows-64K, rows-2M | same | dense | seq, rand |
| 3 | density | How much memory does each layout need for dense, clustered and sparse keys? | flat, rows-4K, rows-64K, rows-2M | 10M, 100M | dense, clustered, sparse | rand |
| 4 | hugepages | Do 2 MiB pages still remove TLB misses at several GB? | flat, rows-64K, rows-2M on huge pages | 10M, 50M, 100M | dense | seq, rand |
| 5 | baselines | Do the hash table and skip list keep up at scale? | hash table, skip list (≤ 10M) | 1M … 100M | dense | seq, rand |

- **Operations:** insert (every key), get_hit, get_miss (keys never written), update. Experiment 3 has no update.
- **Measurement:** 3 repetitions (also in `--quick`). Each repetition measures one of the 3 hardware counter groups,
  so fewer than 3 repetitions would leave some counters unmeasured.
- **Counters:** time, instructions, L1 misses, LLC misses and TLB misses per operation.
- **Gets and updates** use min(keys, 20M) operations.
- **Row sizes and where the row table lives** (2^27 key range, 4 bytes per entry):

| Name | Keys per row | Row size | Table entries | Table size | Fits in |
|---|---:|---:|---:|---:|---|
| rows-4K | 64 | 4 KiB | 2,097,152 | 8 MiB | L3 |
| rows-64K | 1,024 | 64 KiB | 131,072 | 512 KiB | L2 |
| rows-2M | 32,768 | 2 MiB | 4,096 | 16 KiB | L1 |

- **Key patterns:**
  - **dense:** keys 0 … N−1, so every row is full.
  - **clustered:** runs of 1,000 consecutive keys at random places, like block-store writes.
  - **sparse:** N random keys spread over all 134M possible keys.
- **What each report contains:**
  - the memory plan;
  - checks: correctness, O(1) by instruction count, 0 page faults, peak memory;
  - key-generation cost;
  - memory per key;
  - tables by key count;
  - for experiments 2–4, the extra cost of each layout over the flat array.

## What happens in memory, in simple terms

### The rule

The program, plus the one structure that exists at that moment, never uses more than **9 GiB**. If the machine has
less free memory than that, the limit drops automatically to (free memory − 0.5 GiB). **10 GiB is a hard stop**: if
memory is ever measured above it, the program stops at once.

### What is in memory during a run

1. **The program itself.** Up to about 256 MiB is set aside for it, which is generous; it really uses a few MiB.
2. **One structure.** Only one exists at a time. It is created, measured, and destroyed before the next one is
   created. Nothing else ever runs alongside it: the script starts one experiment after another, never two together.
3. **No key lists.** Instead of storing 100M keys (which would cost about 1.6 GB), "the i-th key" is computed when
   needed with a reversible scramble formula. Keys are computed 4,096 at a time into a 16 KB buffer, outside the
   measured sections, so the measurements contain only the structure's work.

### How each structure uses memory

**Flat array:** a numbered shelf with one 64-byte slot for every possible key (134M slots).
- The whole shelf would be 8 GiB, but it is only **reserved**, like booking a hotel floor without receiving the
  rooms.
- The operating system hands over a real room (a 4 KiB page = 64 slots) only when something is first written there.
- With dense keys 0 … 100M−1, about 1.56 million rooms are used: **6.0 GiB**.
- Reading a slot that was never written shows a shared, always-empty room that costs no memory.
- Before measuring, the program writes once into every room that will hold keys, so that no room is handed out (a
  "page fault") in the middle of a timed measurement.

**Rows (the matrix):** a small index box (the row table) plus drawers (rows).
- A drawer holds 64, 1,024 or 32,768 slots.
- The planner first counts exactly how many drawers the key set will need, and only that many are bought up front.
  So the memory is known before anything is measured.
- A drawer is given to a row the first time one of its keys is written.
- Dense keys fill every drawer, so the rows layout uses the same about 6 GiB as the flat array at 100M keys.

**Hash table:** a table whose size is the next power of two at least 1.33 × the keys, 64 bytes per entry.
- At 75M and 100M keys this is 2^27 entries = **8 GiB**, the biggest single structure. It still fits under 9 GiB.

**Skip list:** about 96 bytes per key. It only runs up to 10M keys (about 1 GiB), because at 100M keys one run would
take hours.

### Where the memory goes when keys are not dense (experiment 3)

| Keys | Pattern | flat | rows-4K | rows-64K | rows-2M |
|---|---|---:|---:|---:|---:|
| 10M | dense | 0.6 GiB | 0.6 | 0.6 | 0.6 |
| 10M | clustered | 0.7 GiB | 0.6 | 1.1 | **7.4** |
| 10M | sparse | **8 GiB** | 8 | 8 | 8 |
| 100M | dense | 6.0 GiB | 6.0 | 6.0 | 6.0 |
| 100M | clustered | 6.3 GiB | 6.0 | 7.5 | 8 |
| 100M | sparse | 8 GiB | 8 | 8 | 8 |

(The planner's own figures add 0.25 GiB for the program.)

- **Clustered keys with big drawers waste memory.** A run of 1,000 keys needs only 64 KB but gets a whole 2 MiB
  drawer, about 32× more than needed.
- **Sparse keys put a few keys into every drawer**, so every layout ends up holding the whole 8 GiB range. This is
  the main honest result of experiment 3: on-demand rows only save memory when keys are grouped.
- Every case still fits under 9 GiB. If anything doesn't fit, the report says "skipped" and how much it would have
  needed.

### Peak memory per experiment (full run, 4 KiB pages)

| Experiment | Biggest structure | Peak (program + structure) |
|---|---|---:|
| 1 flat_scale | flat, 100M keys | about 6.3 GiB |
| 2 row_cost | any layout, 100M dense keys | about 6.3 GiB |
| 3 density | sparse keys, or clustered with 2 MiB rows | about 8.3 GiB |
| 4 hugepages | flat, 100M keys, on the huge-page pool | about 6.3 GiB |
| 5 baselines | hash table, 75M and 100M keys | about 8.3 GiB |

### The huge-page phase (experiment 4)

- Huge pages come from a special pool that has to be set aside in advance.
- The script asks the program how many pages experiment 4 needs: about 3,060 pages = 6 GiB for 100M keys.
- It reserves exactly that, runs experiment 4, and gives the memory back. It gives it back even if the run is
  interrupted (Ctrl-C), using a shell `trap`.
- While the pool exists, that memory is unavailable to anything else. That's why the normal-page experiments run in a
  separate phase with the pool released.
- If memory is too fragmented to reserve every page, experiment 4 runs the sizes that fit and marks the rest
  "skipped". Rebooting and re-running `--phase huge` gives a fresh, unfragmented pool.

### The four safety layers

1. **Before creating anything:** compute the exact memory, and skip anything above the budget.
2. **Budget lowered automatically** when the machine has less free memory than the budget.
3. **After creating and after the first insert:** measure real memory (VmRSS + HugetlbPages) and stop at 10 GiB.
4. **The script:** one process at a time; huge pages released before the normal-page phase and returned after
   experiment 4.

## Caveats

1. **Close other programs** (browsers, IDEs). They reduce free memory, and the program then lowers its budget and
   skips the biggest cases.
2. **Swap must not be used.** If `free -h` shows swap growing during a run, the numbers are not valid.
3. **TLB reach.** About 2,000 TLB entries for 2 MiB pages cover about 4 GiB. Above that (75M–100M keys) TLB misses
   return even with huge pages. That is part of what experiment 4 measures.
4. **The row table cannot be pinned in a cache.** Under random access to GBs of data, the data pushes it out. The
   experiments measure where it really lives.
5. **Counters see table and data misses together.** The table's cost is read as "rows minus flat" (experiment 2).
6. **From 10M keys everything is DRAM-bound** (the L3 is 18 MiB). Instructions per operation remain the O(1)
   evidence.
7. **Misses on the flat array are slow when keys are dense.** The absent keys lie across the rest of the 8 GiB range,
   on pages that are never used, so every miss costs a page-table walk. The rows layout answers such a miss from its
   table without touching data. This is a real difference between the layouts, not a measurement error.
8. **Key generation is not in the measurements.** Keys are computed in batches of 4,096 (a 16 KB buffer that stays
   in L1) between measured sections. The timer and hardware counters run only while the structure is being used.
   Reading the buffer adds about 1/16 of an L1 miss per operation. Fixed after the first quick run
   (`results/scale-20261123-200631/analysis.md`), where key generation had added 9–24 ns per operation and made the
   O(1) check fail falsely.
9. **Run time:** expect a few hours for the full run (estimate, not measured). `--quick` is much shorter.
10. **One machine.** Pin the CPU (`--cpu`), keep the frequency fixed if possible, and watch for thermal throttling.
11. **Experiment 6 (threads at 100M keys) is not built yet.** It needs a thread-safe row-creation step; the node
    already reserves `lock` and `version` for it.
