# VDC-Memtable: Design Notes

Status: early research, idea stage. Nothing implemented or measured yet.
Compaction is explicitly out of scope for now.

## Goal

A memtable with:

- O(1) point lookup
- O(1) insert of a new key
- close to O(1) update of an existing key

## REVISED MODEL (supersedes the hashing/probing parts below)

Clarified by the author: the memtable is ONE VDC-Space of 2^20 slots. The key
is an integer in [0, 2^20) and **the slot is `vdc(key)`**: no hashing, no
probing, no collisions.

    slot = rev20(key)            // full 20-bit reversal, bijection on [0, 2^20)
    slot = hi(key) | rev_m(lo)   // partial: reverse only the low m bits, bijection

- Any bit permutation of a 20-bit value is a bijection on [0, 2^20), so two
  distinct keys never share a slot. Collision-freedom comes from the key domain
  being exactly the slot domain, not from VDC itself.
- Lookup, insert, update, delete are all **O(1) worst case** (one address
  computation + one slot access).
- Reversing only m bits (m <= 20) keeps it a bijection and controls scatter
  granularity: the high 20-m bits keep keys in the same region, the low m bits
  are scattered inside it. m = 20 is full VDC, m = 0 is identity.
- If keys have more than 20 bits of variation, NO bit-reversal of m bits can be
  a bijection onto 2^20 slots (pigeonhole). Requires the contract:
  live keys lie in a dense window `[base, base + 2^20)` and `key - base` is used.
- Slot state needs an occupancy flag (and a tombstone flag); occupancy bitmap
  = 2^20 bits = 128 KiB.

### Memory for 2^20 slots (slot size x 2^20)

| slot size | total |
|---|---|
| 8 B | 8 MiB |
| 16 B | 16 MiB |
| 24 B | 24 MiB (25,165,824 B) |
| 64 B | 64 MiB |
| 4 KiB | 4 GiB |
| 64 KiB | 64 GiB |

Allocated for full capacity regardless of how many keys are present.

### What VDC buys here (and what it does not)

- Does NOT improve correctness or speed vs identity mapping (`slot = key`);
  both are collision-free O(1).
- Its only effect is placement: consecutive keys land 2^19, 2^18, ... slots
  apart. Possible benefits: spreads concurrent writers on sequential keys
  across cache lines/pages (less false sharing and contention). Costs: lose
  spatial locality of sequential keys, more pages/cache lines touched, more TLB
  misses.
- Sorted output needs no sort: iterate key = 0..2^20-1, read slot rev(key).
  O(N) but with random access; skip empties with the occupancy bitmap.

## Core idea (ORIGINAL framing: hashed keys, probing)

Use the Van der Corput (VDC) sequence as a slot estimator for a fixed-size
in-memory unit called a **VDC-Space**.

### Van der Corput sequence (reference)

Base 2: write n in binary, mirror the digits around the binary point.

    n : 0   1    2    3     4    5     6     7
    vdc: 0  .1  .01  .11  .001 .101  .011  .111
         = 0, 1/2, 1/4, 3/4, 1/8, 5/8, 3/8, 7/8

On a 10-bit integer this is exactly **10-bit bit reversal**:
`slot = reverse10(n)`. It is a bijection on [0, 1023], is O(1) (a few
shifts/masks, or a 1024-entry lookup table, or `rbit` on ARM), and any prefix of
the sequence is spread evenly over the space (low discrepancy).

### VDC-Space

- 1024 slots, indexed by a 10-bit value (0..1023).
- Slot size is tunable. At 24 bytes/slot the space is 24 KB (1024 x 24 B).
  The slot count stays at 1024 to keep the index 10 bits wide.
- Each slot holds a key (or key reference), a value (or value reference),
  and metadata (state/tag, sequence number, etc.). The exact layout is open.

### Lookup path (target)

1. Hash the key to a 10-bit home slot `h` (and optionally apply the VDC
   mapping, see "Open question: where does VDC actually enter?").
2. Probe linearly from `h`: compare key, stop on match or on an empty slot.
3. On match, the value is in the same slot (no extra indirection).

Expected cost is O(1) for a bounded load factor. Worst case is O(slots),
as for any open-addressing table.

### Collision handling

1024 slots does not mean 1024 storable keys without collisions. Even at low
load, several keys will map to the same home slot. Plan:

- Linear probing from the home slot with a hash/tag stored per slot so most
  mismatches are rejected without a full key compare.
- Cap the fill at a load factor below 1 (for example 0.5 to 0.75), then the
  Space is "full" and a new Space is needed. The useful capacity of one Space
  is therefore roughly 500-750 keys, not 1024.

### Insert / update

- Insert of a new key: compute home slot, probe to first free slot, write.
- Update of an existing key: lookup, then overwrite in place (or append a
  new version, see THINGS_TO_THINK_ABOUT.md, section 7).

## Where VDC is proposed to be used (to be decided)

Possible roles, not mutually exclusive:

1. **Slot mapping:** `slot = vdc(hash(key) mod 1024)`. (See caveat below.)
2. **Probe sequence:** the i-th probe for a key goes to
   `(h + reverse10(i)) mod 1024` instead of `h + i`, to avoid primary
   clustering while keeping probes spread across the table. (Because
   reverse10 is a bijection, probes i = 0..1023 visit every slot once.)
3. **Sequence-number placement:** place by insertion counter
   `slot = vdc(counter)` (only meaningful if a separate index maps key to
   counter, so probably not useful for lookup).

Role 1 on its own is a bijection of a hash, so it changes nothing statistically
if the hash is already uniform. It only helps for inputs that are NOT hashed
(sequential or low-entropy integer keys). Role 2 is the more plausible
contribution. This has to be validated experimentally (see THINGS_TO_THINK_ABOUT.md).

## Scaling to 2^20 slots (20-bit slot id)

Split the 20-bit id into **10 bits node id + 10 bits slot-in-node**.

- 2^20 slots / 1024 slots per node = 1024 nodes.
- Node = 64 KiB means 64 B per slot (one cache line per slot).
- Memtable = 1024 nodes x 64 KiB = **64 MiB** (2^20 x 64 B), the same as
  RocksDB's default write buffer.
- If the slot is 24 B instead: 2^20 x 24 B = 24 MiB. A 64 KiB node then holds
  2730 slots, which does not match a power-of-two id, so 64 B slots is the
  cleaner fit.

Usable entries are fewer than 2^20 (see collision section below).

## Collision-free: what is and is not possible

- Pigeonhole + birthday: n random keys into m = 2^20 slots have no collision
  with probability about exp(-n^2 / 2m). That is 50% at only ~1200 keys. So
  "no collisions" from hashing alone is impossible at memtable sizes.
- Fully collision-free (key = slot) only works if the key domain is <= 2^20
  (direct addressing). VDC then merely permutes ids.
- Perfect hashing needs the key set in advance. A memtable is dynamic, so it
  can only be applied at seal time (immutable table, lookup-only).
- Practical target: **no unbounded probing**, i.e. worst-case O(1) lookup:
  cuckoo hashing with 2 candidate buckets, bucket = several entries.
  Max load before insert failure: 1 entry/bucket ~50%, 2 ~90%, 4 ~97%.
  Lookup touches at most 2 buckets. Insert is amortized O(1) expected;
  on failure, spill to a stash/next node or seal the node.
- Both candidate buckets must come from independent hash bits (e.g. two
  different 20-bit fields of one 64-bit hash). h2 = reverse20(h1) is NOT
  independent: keys colliding on h1 also collide on h2.
- Always store the full key (or fingerprint + key ref) in the entry, since two
  different keys can share the same 20-bit id.

## Non-goals (for now)

- Compaction strategy
- Persistence / WAL format
- Range scans (see open problems)

## Experiments to run first

1. Simulation: probe-length distribution vs load factor for
   (a) linear probing, (b) quadratic, (c) VDC probe sequence, (d) Robin Hood,
   across uniform hashed keys, sequential integer keys, and skewed keys.
2. Microbenchmark of one Space vs `std::unordered_map`, a skiplist, and a
   flat open-addressing table (e.g. Swiss table style).
3. Cache behaviour: a 24 KB Space fits in L1d on most cores; measure whether
   VDC probe jumps hurt versus the contiguous scan of linear probing.

## Engineering notes: cache alignment and locality (revised model)

- Compare against the right baselines. Versus identity mapping (`slot = key`)
  VDC has the same O(1). Versus skiplist/B-tree memtables (O(log n), many
  dependent pointer loads) direct addressing is a real win, but that win
  comes from direct addressing, not from the VDC permutation.
- Other memtables with O(1): hash memtables (expected O(1)), vector memtable
  (O(1) append, no lookup). Direct-addressed VDC-Space is O(1) worst case for
  get/put/update/delete and yields sorted output without a sort.
- Slot size: use a power of two dividing 64 B (32 B or 64 B), so no slot
  straddles a cache line. 24 B does not divide 64. 32 B x 2^20 = 32 MiB,
  64 B x 2^20 = 64 MiB.
- Align the base to the cache line, ideally to 2 MiB and back it with huge
  pages: 64 MiB is only 32 huge pages, which makes the TLB cost of scatter
  negligible.
- Partial reversal as the locality knob: with s = 2^j slots per cache line,
  keep the low j key bits unchanged and reverse only the upper 20-j bits.
  Keys sharing a line stay together; lines are scattered.
  `slot = rev_{20-j}(key >> j) << j | (key & (2^j - 1))`
- Pad/align per-line metadata so writers on different lines never false-share.
- Occupancy bitmap (128 KiB) fits in L2; hierarchical summary for sparse scans.
