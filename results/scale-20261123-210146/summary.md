# Scale experiments: summary

Combined from the per-experiment CSV files. Times are ns per operation (medians), random order unless stated. Per-experiment reports (e1_*.md ... e5_*.md) have every table.

### Overview

| experiment | structures run | skipped (memory / time) | peak memory measured | wrong results |
|---|---|---|---|---|
| 4 | 9 | 0 | 5.96 GiB | ✅ 0 |

### 4. Huge pages vs base pages: get_hit, dense keys, random order

| keys | structure | base ns | hugetlb ns | base dTLB/op | hugetlb dTLB/op |
|---|---|---|---|---|---|
| 10M | flat | - | 12.3 | - | 0.000 |
| 10M | rows-64K | - | 18.0 | - | 0.000 |
| 10M | rows-2M | - | 16.9 | - | 0.000 |
| 50M | flat | - | 12.4 | - | 0.464 |
| 50M | rows-64K | - | 18.2 | - | 0.462 |
| 50M | rows-2M | - | 17.2 | - | 0.467 |
| 100M | flat | - | 12.6 | - | 0.736 |
| 100M | rows-64K | - | 18.9 | - | 0.728 |
| 100M | rows-2M | - | 17.6 | - | 0.738 |

### O(1) check: worst max/min instructions per op across key counts (≤ 1.10)

| structure | pages | worst ratio | O(1) |
|---|---|---|---|
| flat | hugetlb | 1.00 | ✅ |
| rows-2M | hugetlb | 1.00 | ✅ |
| rows-64K | hugetlb | 1.00 | ✅ |
