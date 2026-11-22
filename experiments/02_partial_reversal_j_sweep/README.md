# 02 Partial reversal j sweep

**Question**: which j (6, 8, 11, 15, 20) gives the best cache behaviour? Covers P11 (locality), P16 (cache-set
aliasing) and P18 (sequential access).

## Analytic part

```bash
./build/exp_02_partial_reversal_j_sweep
```

It reads this machine's cache geometry from sysfs, or you can pass `--l1-sets 64 --l1-ways 12 --l2-sets 2048 --l2-ways 16 --line 64`.
For windows of W consecutive keys it prints the largest number of keys that share one cache set in L1D and in L2.
❌ means more keys than ways, so conflict misses happen even though the cache has room. The L2 numbers are exact
with 2 MiB pages.

**Expected**: for j = 6, L1 is fine but L2 conflicts start at windows of about 1K+ keys. Raising j (around 11) covers
the L2 set-index bits.

## Measured part

The main benchmark matrix is the j sweep. Every table in `report_vdc.md` has one column per j:

```bash
./build/vdc_bench --impl vdc --j 6,8,11,15,20 --pages base,hugetlb --csv j_sweep.csv > j_sweep.md
```

Look at `dense_rand` and `seq` (dense key ranges): L1D and LLC misses/op per j, and "Fastest variant per workload".
For `rand` keys j should not matter much, since any permutation of random keys is random.
