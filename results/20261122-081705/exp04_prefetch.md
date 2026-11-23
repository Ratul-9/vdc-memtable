# Experiment 04: prefetch distance for batched gets

1000000 keys, j = 6, median of 7 runs. Cell = ns/op (speedup vs no prefetch).

| prefetch distance | base / dense_rand | base / rand | hugetlb / dense_rand | hugetlb / rand |
|---|---:|---:|---:|---:|
| 0 (no prefetch) | 18.6 (1.00x) | 18.7 (1.00x) | 17.4 (1.00x) | 17.2 (1.00x) |
| 1 | 25.3 (0.73x) | 22.5 (0.83x) | 19.9 (0.88x) | 19.7 (0.87x) |
| 2 | 20.7 (0.90x) | 19.8 (0.94x) | 17.1 (1.02x) | 16.9 (1.02x) |
| 4 | 16.5 (1.13x) | 16.3 (1.15x) | 14.8 (1.18x) | 13.8 (1.25x) |
| 8 | 13.0 (1.43x) | 13.0 (1.44x) | 10.8 (1.60x) | 10.3 (1.67x) |
| 16 | 9.2 (2.03x) | 8.9 (2.11x) | 8.3 (2.11x) | 7.8 (2.21x) |
| 32 | 8.4 (2.20x) | 8.1 (2.30x) | 7.5 (2.32x) | 7.1 (2.43x) |
| 64 | 8.9 (2.08x) | 8.3 (2.25x) | 7.6 (2.29x) | 7.3 (2.36x) |
| memtable_get_batch (16) | 9.3 (2.00x) | 9.4 (1.98x) | 8.4 (2.06x) | 8.1 (2.11x) |
