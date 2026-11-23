# Analysis of the quick scale run (scale-20261123-200631)

Machine: i5-12400F, 18 MiB L3, 16 GB RAM. Quick mode:
- keys 1M, 10M, 25M (density and huge pages at 10M only);
- 2 repetitions.

## 1. What worked

| Check | Result |
|---|---|
| Unit tests on Linux | 10/10 |
| Correctness, all 5 experiments | ✅ every get returned its own key |
| Page faults during measurements | ✅ 0 |
| Memory: peak per experiment | E1 1.49, E2 1.50, **E3 8.00**, E4 0.60, E5 2.00 GiB; all under 9 GiB |
| Memory planner accuracy | measured = planned − 0.25 GiB program allowance, to within 0.01 GiB (e.g. rows-2M clustered: planned 7.69, measured 7.45) |
| Skips | only the skip list at 25M (time limit), as designed |

**The memory safety works.** The planner's predictions matched reality, and the largest case reached 8.00 GiB.

## 2. Two benchmark problems found (they affect the full run that is running now)

**A. Key generation is inside the timed loops, and it is expensive.**

| pattern | random-order key generation (ns/key, 10M) |
|---|---:|
| dense | 9.1 |
| clustered | **23.6** |
| sparse | 10.2 |

- A random get on the flat array measured 24 ns, of which about 9 ns was computing the key.
- Clustered looked slower than dense (41 vs 24 ns), but mostly because its keys cost 23.6 ns to compute. That comes
  from a division by the run length plus extra rounds of the scramble.

**B. That cost changes with the key count, which made the O(1) check fail falsely.** The scramble works on the next
power of two above N and repeats until the result is below N, so it needs about 2^b / N rounds:

| N | power of two | rounds |
|---|---:|---:|
| 1M | 2^20 | 1.05 |
| 10M | 2^24 | **1.68** |
| 25M | 2^25 | 1.34 |

Instructions per operation follow exactly this pattern, identically for every structure:

| op (random order) | 1M | 10M | 25M |
|---|---:|---:|---:|
| flat get_hit | 80.8 | 90.3 | 85.3 |
| rows-2M get_hit | 94.7 | 103.9 | 99.0 |

In sequential order (key = i, no scramble), instructions are **exactly constant** for every structure:

| op (sequential) | 1M | 10M | 25M |
|---|---:|---:|---:|
| flat insert / get_hit / update | 28.0 / 58.0 / 31.0 | 28.0 / 58.0 / 31.0 | 28.0 / 58.0 / 31.0 |
| rows-* insert / get_hit / update | 42.0 / 72.0 / 45.0 | 42.0 / 72.0 / 45.0 | 42.0 / 72.0 / 45.0 |

**The structures are O(1); only the key generator is not constant.** Differences between structures are still
valid, because every structure pays the same key cost.

**C. In quick mode the TLB counters were never measured.** Each repetition measures one of 3 counter groups, and
quick mode has 2 repetitions. The full run (3 repetitions) is not affected. The summary printed 0.000 instead of "-"
for the missing values.

**Fix:**
- generate keys in batches of about 64K outside the timed region (256 KB of memory), so key cost leaves both time
  and instruction counts;
- use a power-of-two run length (1024) for clustered keys;
- show "-" for missing counters in the summary.

## 3. Results that are valid despite A–C

### Row-table cost (experiment 2), get_hit on dense keys, random order

| keys | flat | rows-4K (table in L3) | rows-64K (table in L2) | rows-2M (table in L1) |
|---|---:|---:|---:|---:|
| 1M | 19.4 | +4.7 ns | +3.7 | +3.7 |
| 10M | 23.9 | +6.8 | +4.4 | **+4.0** |
| 25M | 22.1 | +7.4 | +5.2 | **+4.2** |

- **Sequential order:** only +0.2 to +0.3 ns for every row size.
- **The table adds 14 instructions per operation**, constant (72 vs 58 for a sequential get).
- **The row table never misses the LLC** (Δ LLC = 0.00). Its cost is the dependent extra load plus 14 instructions.
- **Where the table lives, from L1D misses per get on huge pages (so no page-walk noise):**

| | flat | rows-2M | rows-64K |
|---|---:|---:|---:|
| L1D misses per get | 1.00 | **1.01** | 1.62 |

  The rows-2M table really stays in L1 (+0.01 misses); the rows-64K table costs about 0.6 extra L1 misses (it lives
  in L2). On base pages, rows-4K adds about 1 L1 miss.
- **Net of key generation, the 2 MiB layout costs about +27% on random gets** (about 14.8 → 18.8 ns at 10M), and
  almost nothing on sequential access.

### Misses (absent keys): rows are faster

| keys | flat | rows-2M |
|---|---:|---:|
| 1M | 15.0 | **3.9** |
| 10M | 15.4 | **11.6** |
| 25M | 12.8 | **9.6** |

Absent dense keys have no row, so the rows layout answers from its table without touching data.

### Memory per key (experiment 3, 10M keys)

| pattern | flat | rows-4K | rows-64K | rows-2M |
|---|---:|---:|---:|---:|
| dense | 64 B | 65 | 64 | 64 |
| clustered (runs of 1,000) | 67 | 68 | 124 | **799** (12.5× waste) |
| sparse | 853 | 854 | 859 | 859 |

- Dense: every layout reaches the 64 B minimum.
- **Clustered: small rows are needed.** 2 MiB rows hand a whole 2 MiB drawer to a 64 KB run.
- **Sparse: no layout saves memory.** 10M random keys spread over the 134M range touch almost every page and row,
  about 8 GiB, 13× more than the same keys stored dense.
- The flat array on base pages already gets on-demand memory "for free" from the operating system, at 4 KiB
  granularity, so rows-4K matches it exactly.

### Huge pages (experiment 4, 10M keys, random get)

| | base | hugetlb |
|---|---:|---:|
| flat | 23.9 ns | 21.6 (−10%) |
| rows-2M | 27.9 | 25.2 (−10%) |

L1D misses per get fall from about 2.0 to 1.0: huge pages remove the page-table loads.

### Baselines (experiment 5, random order, includes about 9 ns of key generation)

| 10M keys | flat | rows-2M | hash table | skip list |
|---|---:|---:|---:|---:|
| get_hit | 23.9 | 27.9 | 59.0 | 1,544 |
| insert | 14.3 | 14.9 | 54.7 | 1,444 |
| get_miss | 15.4 | 11.6 | 118.1 | 54.5 |
| LLC misses per get | 1.96 | 1.96 | 8.46 | 60.3 |

- Net of key generation: the hash table about 3.4× and the skip list about 100× slower than flat on gets.
- The hash table's misses grow with load: 58 → 118 → 195 ns from 1M to 25M.

## 4. Recommendation

The base phase running now uses the same key generator. Its relative results (row-table cost, memory per key, the
baseline ranking) will be valid. Its absolute times, the O(1) check and clustered-vs-dense comparisons will carry the
same distortion.

**Stop it, apply the fix, and re-run.** The fix takes a short time; the full run takes hours, so running it once with
clean numbers is the better use of the machine.

## 5. Status of the fix

Done:
- keys are computed in batches of 4,096 outside the measured sections, and the timer and counters are summed over
  the batches;
- `--quick` now uses 3 repetitions, so every counter group is measured;
- the summary shows "-" when a counter was not measured.

The results in this folder were produced before the fix; re-run to replace them.
