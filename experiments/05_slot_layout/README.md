# 05 Slot layout

**Question**: how does the decided 64 B Node map onto this machine's cache lines and pages (P5, P6, P12)?

**Run**:

```bash
./build/exp_05_slot_layout
```

**Output**: the field / offset / size table, Nodes per cache line (1 on 64 B-line CPUs, 2 on 128 B-line CPUs such as
Apple M-series), Nodes per page, and the Space size in base and huge pages.

The layout itself is fixed by static asserts in `include/vdc/slot.h` and checked by `tests/test_node.c`.
