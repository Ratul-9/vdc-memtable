# Experiment 05: Node layout

| Field | Offset | Size | Purpose |
|---|---:|---:|---|
| key | 0 | 3 B | 20-bit key stored in 24 bits |
| reserved | 3 | 5 B | empty, kept zero |
| seqno | 8 | 8 B | write sequence number |
| ext_off | 16 | 8 B | arena offset of a value > 32 B |
| val_len | 24 | 4 B | value length |
| flags | 28 | 4 B | OCCUPIED, TOMBSTONE, EXTERNAL |
| value | 32 | 32 B | inline value |

| Property | Value |
|---|---|
| sizeof(Node) / alignment | 64 B / 64 B |
| Cache line on this machine | 64 B |
| Nodes per cache line | 1 |
| Base page | 4096 B = 64 Nodes |
| 2 MiB huge page | 32768 Nodes |
| Slots | 1048576 |
| Space size | 67108864 B = 64 MiB = 16384 base pages = 32 huge pages |
