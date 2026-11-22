# 03 Huge pages and TLB misses

**Question**: do 2 MiB hugetlb pages remove the TLB misses of a 64 MiB VDC-Space (P14, P15, P4)?

64 MiB on 4 KiB pages = 16,384 pages, far more than the TLB holds. On 2 MiB pages it is 32 pages.

**Run** (Linux, after `sudo scripts/setup_hugepages.sh`):

```bash
./build/vdc_bench --impl vdc --pages base,hugetlb --patterns rand,dense_rand --csv tlb.csv > tlb.md
```

**Read**:

- **Checks → TLB**: dTLB misses/op on hugetlb must be ≤ 0.05.
- **Huge pages vs base pages**: per op and j, dTLB misses/op and ns/op shown as base → hugetlb at 1M keys.
- **Results by workload → dTLB misses per operation**: how it grows with N on base pages.

**Expected**: on base pages with `rand` keys at 1M, close to 1 dTLB miss per op (each op touches a new page). On
hugetlb, close to 0, with lower ns/op. At 1K keys there is no difference.

This experiment uses `vdc_bench`, so it has no program of its own.
