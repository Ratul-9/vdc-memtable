# 06 Fill and rotation costs

**Question**: what does it cost to create, fill, reuse and drop one 64 MiB memtable (P1, P2, P9, P10, P28)? These are
the building blocks of rotation, which is not implemented yet.

**Run**:

```bash
./build/exp_06_fill_and_rotation --keys 1000000 --pages base,hugetlb
```

**Output**: for each page mode with populate off and on, the table shows:

- create time
- resident memory (VmRSS, HugetlbPages) after create
- fill ns/op and minor page faults during the fill
- resident memory after the fill
- clear time (`memtable_clear`, reuse without unmapping)
- destroy time

**Expected**:

- Populate moves the page faults from the first fill into create.
- Lazy hugetlb faults about 32 times; lazy base pages fault about 16,384 times.
- Clear (a 64 MiB memset) costs a few milliseconds. That is the price of reusing a Space from a pool instead of
  mapping a new one.
