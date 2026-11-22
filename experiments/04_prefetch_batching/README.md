# 04 Prefetch batching

**Question**: how much does software prefetch help batched gets, and what prefetch distance is best (P17)?
The key → slot address needs no memory load, so the Node of key i + d can be prefetched while key i is processed.

**Run**:

```bash
./build/exp_04_prefetch_batching --keys 1000000 --j 6 --pages base,hugetlb
```

**Output**: rows are prefetch distances 0 (none), 1, 2, 4, 8, 16, 32, 64, plus the library `memtable_get_batch`
(distance `VDC_PREFETCH_DISTANCE` = 16). Columns are page mode × pattern. Each cell shows ns/op and the speedup over
no prefetch.

**Expected**: no gain at small N (data is in cache). At 1M random keys, a large gain that levels off around 8–32. If
the best distance is far from 16, change `VDC_PREFETCH_DISTANCE` in `include/vdc/config.h`.

`vdc_bench` also reports `get_batch` next to `get_hit` for every cell.
