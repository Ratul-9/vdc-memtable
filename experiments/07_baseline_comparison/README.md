# 07 Baseline comparison

**Question**: how does the VDC memtable compare with a hash table (expected O(1)) and a skip list (O(log n), the
RocksDB default)? Also, does the VDC permutation cost anything compared with identity mapping (j = 20)?

**Run**:

```bash
./build/vdc_bench --impl vdc,hash,skiplist --j 6,11,20 --pages base,hugetlb \
    --ops insert,get_hit,get_miss,update,delete --csv baselines.csv > baselines.md
```

All three use the same 32 B values, the same tombstone semantics and the same page modes. The hash table has 2^21
entries (load ≤ 0.5 at 1M keys). The skip list allocates its nodes from a populated arena.

**Read**:

- **Checks → O(1)** per structure: vdc should pass. The skip list fails (instructions grow with log N). The hash
  table may drift as its load factor rises.
- **Results by workload**: ns/op and misses/op per structure for each key count.

**Expected**: vdc ≤ hash at every N (no hashing, no probing). The skip list is several times slower at 1M keys, with
several LLC misses per op.

This experiment uses `vdc_bench`, so it has no program of its own.
