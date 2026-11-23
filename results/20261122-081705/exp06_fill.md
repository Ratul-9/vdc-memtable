# Experiment 06: create / fill / clear / destroy cost

1000000 keys inserted in random order, 32 B inline values. RSS = VmRSS, hugetlb = HugetlbPages (from /proc/self/status; n/a elsewhere).

| pages | populate | create ms | RSS after create MiB | hugetlb after create MiB | fill ns/op | minor faults during fill | RSS after fill MiB | clear ms | destroy ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| base | no | 0.01 | 5.5 | 0.0 | 56.2 | 31250 | 66.5 | 9.25 | 1.91 |
| base | yes | 18.02 | 69.6 | 0.0 | 30.2 | 0 | 69.6 | 8.45 | 2.12 |
| hugetlb | no | 0.01 | 5.6 | 0.0 | 36.7 | 64 | 5.6 | 8.55 | 0.03 |
| hugetlb | yes | 8.52 | 5.6 | 64.0 | 27.0 | 0 | 5.6 | 8.75 | 0.02 |

Expected: populate moves the page-fault cost from the first fill into create. With hugetlb, a lazy (populate = no) fill faults about 32 times (one per 2 MiB page) instead of about 16,384 (one per 4 KiB page).
