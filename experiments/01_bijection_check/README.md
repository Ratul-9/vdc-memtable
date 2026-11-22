# 01 Bijection check

**Question**: is every key → slot mapping we test (j = 0, 6, 8, 11, 15, 20) a bijection on 0 .. 2^20-1, and how many
pages does a dense range of keys touch for each j?

**Run** (no special hardware, also works on macOS):

```bash
./build/exp_01_bijection_check
```

**Output**: one table that says bijective / involution per j, and two tables of 4 KiB and 2 MiB pages touched by keys
0..N-1 for N = 1K, 10K, 100K, 1M. Fewer pages means fewer TLB entries for a dense working set.

**Expected**: every j bijective. Full reversal (j = 0) touches the most pages; j = 20 (identity) touches the minimum.
Exit code 1 if any mapping fails.
