# Concurrent VDC memtables: design and test plan

Goal: measure how the VDC memtable scales with threads under eleven designs, and whether VDC's scatter (j < 20)
beats identity mapping (j = 20) once several threads touch neighbouring keys.

## Designs

### Direct: every thread calls the memtable (`include/vdc/concurrent.h`)

| Name | Get | Put / delete | Shared state per op |
|---|---|---|---|
| `mutex` | lock one global mutex | same | the mutex line (every op) |
| `spin` | spinlock of the stripe (64 slots = 4 KiB of Nodes) | same | the stripe's lock line + the Node |
| `rwlock` | shared read lock of the stripe | exclusive write lock | the stripe's lock line + the Node |
| `nodelock` | lock byte inside the Node (`Node.lock`) | same | the Node only |
| `seqlock` | optimistic: read `Node.version`, copy, re-check; no writes | CAS `Node.version` to odd, write, release even | the Node only; readers never write |

- Lock words of the striped designs are padded to 64 B (16,384 stripes = 1 MiB), so neighbouring stripes never
  false-share.
- `seqlock` readers never block and never write shared memory. Writers to the same key still wait for each other
  (a 64 B Node cannot be replaced with one atomic instruction). So reads are lock-free, writes are per-node exclusive.
- All Node fields are read and written as relaxed atomic words. ThreadSanitizer reports no races (`make tsan`).

### Delegation: producers send requests to key owners (`include/vdc/service.h`, `include/vdc/queue.h`)

| Name | Consumers | Queues | Routing |
|---|---|---|---|
| `mpsc` | 1, owns all keys | 1 MPSC | everything to the one consumer |
| `sharded` | C, consumer i owns keys [i·2^20/C, (i+1)·2^20/C) | one MPSC per consumer | producer routes by key range |
| `mpmc` | C, same key ranges | one shared MPMC + one MPSC inbox per consumer | any consumer pops; a foreign key is forwarded to its owner's inbox |

- Queues: Dmitry Vyukov's bounded lock-free ring. Each 64 B cell holds a 56 B request (op, key, seqno, 32 B
  value, completion pointer) and a sequence number.
- The owner does every operation on its keys (insert, update, delete, read), so no locks are needed and no two
  threads ever touch one Node.
- Results come back through a 64 B completion record:
  - `-sync`: the producer waits for every result (round trip).
  - `-async`: the producer keeps up to `--window` (32) requests in flight and never two on the same key.
- `mpmc` forwarding can reorder two in-flight requests on the same key. The async window rule avoids it in the
  benchmark; the unit tests document it.
- Consumers busy-poll (one dedicated core each), like p2KVS. Their threads count in the thread budget.

### Not shared on purpose

- **Sequence numbers** are passed in by the caller. A shared atomic counter would be the bottleneck (P23); in an LSM
  the write path assigns them.
- **Counters** (entries, tombstones) are not maintained live; `cm_count` / `sv_count` scan the Space when idle.
- Values are inline only (≤ 32 B); the arena is not thread-safe.

## Benchmark (`build/vdc_mtbench`)

| Dimension | Values |
|---|---|
| Designs | the 11 above |
| Threads (total, consumers included) | 1, 2, 4, 6, 8, 12 (delegation needs ≥ 2) |
| Consumers (sharded / mpmc) | half of the threads (`--consumers` to fix) |
| j | 6, 11, 20 |
| Workloads | load (insert every key into an empty table), A 50/50 get/update, B 95/5, C 100% get, W 100% update |
| Key distributions | uniform; zipf 0.99 (hottest keys 0, 1, 2, …: adjacent hot Nodes); interleaved (worker w uses w, w+W, w+2W, …: all workers on neighbouring keys) |
| Keys | 1M, all present before A/B/C/W |
| Per cell | 500K ops per worker or producer, 3 repetitions (median), latency from 1 in 64 ops |

**Checks**: every get's value has four equal 64-bit words containing the key, so a torn or misrouted read is
detected. Wrong statuses are counted, and after every load every key must be present. All must be 0.

**Report tables**:
- Checks.
- Fastest design per workload.
- Scaling (max threads / min threads).
- VDC scatter vs identity at max threads.
- For every workload × distribution: throughput, p50, p99 and p99.9 latency by thread count.
- Throughput for the other j values.

## What each comparison answers

| Question | Where to look |
|---|---|
| Do locks or delegation scale better? | Scaling table; throughput by thread count |
| Price of a global lock | `mutex` vs everything |
| Do striped locks cost a second cache miss? | `spin` / `rwlock` vs `nodelock` (lock word in its own line vs inside the Node) |
| Do lock-free reads pay off? | `seqlock` vs `nodelock` on B and C (readers that write the lock line vs readers that do not) |
| Hot keys (P22) | zipf columns: locks queue up on one Node; delegation serialises at one owner |
| Is the delegation round trip worth it? | `-sync` vs `-async`; delegation vs direct at the same thread count |
| Shared MPMC vs per-consumer queues | `mpmc` vs `sharded` (contention on one queue + forwarding cost) |
| Does VDC scatter beat identity? (the novel claim) | "VDC scatter vs identity" table, interleaved and zipf rows, especially `spin` / `rwlock` (64 neighbouring keys share one stripe lock under identity) |

**Hypothesis to test**: with j = 20, interleaved writers hit the same stripe lock and neighbouring cache lines, so
j = 6 (which spreads neighbouring keys over different 4 KiB regions) should win for `spin` / `rwlock` / `nodelock`.
j = 11 keeps runs of 2,048 consecutive keys together, so it may behave like identity here. That is the trade-off
against the single-thread result, where larger j was better.

## Running

```bash
sudo scripts/setup_hugepages.sh
```

```bash
scripts/run_mt_bench.sh
```

`--quick` gives a short run. Results go to `results/mt-<timestamp>/report_mt.md`.

Notes:
- **Thread pinning:** threads are pinned one per physical core first, then onto hyper-thread siblings. On a 6-core
  i5-12400F, 8 and 12 threads share cores; read those rows with that in mind.
- **Hardware counters:** this driver reports time and latency only. For cache-line contention use
  `perf c2c record ./build/vdc_mtbench …` (HITM events), and `perf stat` for totals.
- **macOS:** the driver runs, but without pinning, and latency there has 1 µs timer resolution. Use Linux numbers only.
