# Things to Think About (problems and open questions)

Compaction is out of scope. Items are roughly ordered by how likely they are to
break the idea.

## 1. Where does VDC actually add value?

- `vdc(x)` is bit reversal, a bijection on 10-bit values. Applied to a
  uniformly hashed key it gives an equally uniform slot, i.e. no gain over
  using the hash directly.
- It only helps when keys are not hashed first (sequential IDs, timestamps):
  bit reversal spreads consecutive integers far apart. But a decent hash does
  that too, and sequential locality (good for caches) is lost.
- As a probe sequence it may reduce clustering vs linear probing, but it
  costs cache locality, since linear probing's strength is that it walks
  adjacent memory. Needs measurement, not assumption.
- Compare against cheap baselines: Fibonacci hashing, double hashing,
  Robin Hood, Swiss-table style tag groups.

## 2. "1024 slots" is not "1024 keys"

- Linear probing degrades sharply with load factor. Expected probes for a
  successful search ~ 0.5 (1 + 1/(1-a)); for unsuccessful ~ 0.5 (1 + 1/(1-a)^2).
  At a = 0.9 an unsuccessful search averages ~50 probes; at a = 0.5 about 2.5.
- So usable capacity is ~500-750 keys per Space, and a "full" threshold must
  be defined.
- Birthday effect: with n keys in 1024 slots, collisions occur long before
  full. Quantify the probe-length tail (p99, max), not just the mean.

## 3. O(1) is expected-case, not worst-case

- Lookup is O(1) expected for bounded load; worst case O(1024) within a Space.
  Adversarial or skewed keys (same hash prefix) break it. Is a seeded hash
  needed to prevent hash flooding?
- Be honest in claims: "O(1) expected", and a skiplist at 1024 entries is only
  ~10 levels, so constant factors dominate at this scale.

## 4. One Space is tiny; a memtable is big

- A real memtable is tens of MB to GBs. 1024 slots = a leaf unit.
- Need a top-level structure that routes a key to the right Space:
  directory by hash prefix (extendible hashing), or a list of Spaces by
  fill order, or a hierarchical VDC-of-Spaces.
- If routing needs a tree or a search, the O(1) claim moves to that layer.
  A hash-prefix directory keeps O(1) but doubles/splits on growth (insert is
  then amortized O(1), not strict O(1)).
- What happens when a Space fills: split (rehash half the keys), chain a new
  Space, or freeze it. Splitting costs O(1024) in one insert (latency spike).
- Resize/rehash cost and tail latency vs a pre-sized arena.

## 5. Ordering: range scans and flush

- Hashed placement destroys key order. LSM flush to an SSTable needs sorted
  output; iterators and range scans need order.
- Flush becomes sort of all entries: O(n log n) at flush time, and iterators
  need a copy-and-sort (this is exactly the documented weakness of RocksDB's
  hash-based memtables).
- Alternative: make slot placement order-preserving (e.g. top key bits) so a
  scan is a slot walk; but then skewed key distributions cluster and collide
  heavily. Could a hybrid (hash for point, sorted side-index built lazily at
  freeze time) work? Cost of building it at seal time should be measured.

## 6. Variable-length keys and values

- A 24-byte slot cannot inline arbitrary keys/values. Options: pointer into an
  arena (extra cache miss per compare), inline small keys + fingerprint,
  or store hash + offset only.
- Key compare on probe may touch the arena. Store an 8-16 bit tag in the slot
  to avoid most of these.
- Slot layout needs to be pinned down (for 24 B: 8 B hash/tag+state, 8 B key
  ref, 8 B value ref + seqno packed?).

## 7. Updates, versions, MVCC, snapshots

- LSM memtables normally keep multiple versions (key, seqno) for snapshot
  reads and for correct ordering on flush. In-place update overwrites old
  versions and breaks snapshot isolation.
- Options: keep newest version only (restricts snapshots), version chain per
  slot (not O(1) read of older versions), or append new entry (consumes slots
  fast; load factor grows with update-heavy workloads).
- "Close to O(1) update" needs a definition: in-place overwrite of a fixed-size
  value is O(1); a larger value or a version chain is not.

## 8. Deletes and tombstones

- Open addressing and deletion: removing a slot breaks probe chains. Use
  tombstone markers (these count toward load factor) or backward-shift
  deletion (Robin Hood style).
- In an LSM, deletes must also be written as tombstones that shadow older data
  in lower levels, so they are live entries, not removals.

## 9. Concurrency

- Skiplist memtables get lock-free concurrent inserts. A flat probing table
  needs CAS on slots; linear probing chains make concurrent insert/lookup
  tricky (a reader may see a half-written slot or miss a key moved by a
  concurrent insert).
- Per-Space locks? Writer-only lock with lock-free readers (single-writer
  memtables are common)? Needs a decision early since it shapes slot layout
  (atomic publish word per slot).

## 10. Memory and hardware effects

- Fixed 24 KB per Space is allocated up front, even if mostly empty (wasted
  memory for small workloads, and many Spaces mean many cold cache lines).
- Random placement means each lookup is ~1 cache miss for the home slot at
  scale (not for one Space in L1, but for thousands of Spaces). Skiplists cost
  more misses per op, but a good hash table has no magic here.
- VDC probe jumps may touch multiple cache lines per probe. Linear probing
  usually stays within 1-2 lines.

## 11. Validation of the "novelty" claim

- Compare to what already exists (see the assessment in the chat summary):
  RocksDB HashSkipList/HashLinkList, Speedb sorted hash memtable, FloDB,
  split-ordered lists (bit-reversed keys), Fibonacci hashing,
  quasi-random/low-discrepancy hashing literature.
- Need a benchmark harness (db_bench style workloads: fillseq, fillrandom,
  readrandom, overwrite, readwhilewriting, YCSB A-F) so claims are measurable.

## 12. Definitions to pin down

- What is the input to the VDC function: hash bits, key bits, insertion count,
  or probe index?
- What does "O(1) insert" mean when a Space fills or splits (amortized vs strict)?
- Target key/value sizes and typical entries per memtable.
- Single-threaded or multi-threaded writer?

## 13. Problems specific to the revised direct-addressed model (2^20 slots)

- **Key domain restriction.** Works only for integer keys in a dense 2^20
  window. Real LSM keys are variable-length strings. Mapping strings to ids
  needs a hash (collisions return) or a dictionary/trie (extra lookups, and no
  longer a pure O(1) address computation).
- **Fixed capacity = fixed memory.** 2^20 slots allocated regardless of how
  many keys exist; a sparse memtable wastes (virtual or resident) memory.
- **Scatter touches more pages.** With full reversal, 1000 consecutive keys
  hit ~1000 different regions, so resident pages and TLB pressure grow vs
  identity mapping. Quantify; choose m to trade locality vs scatter.
- **Is VDC needed at all?** Identity mapping is equally collision-free. The
  case for VDC must rest on placement effects (contention, cache-line spread),
  not on correctness.
- **Slot size is the big knob.** 24 B means values are small or out-of-line.
  A 64 KiB slot would be 64 GiB, which is not a memtable.
- **Versions/tombstones** still need a plan (one version per slot vs chains).
- **Ordered scan** is O(capacity) unless an occupancy bitmap (hierarchical)
  is used to skip empty ranges, and each step is a random access.
