/*
 * Identity mapping baseline (slot = key).
 *
 * This is exactly the VDC memtable with j = 20 (no bits reversed), so there is
 * no separate implementation: the benchmarks include j = 20 in every j sweep,
 * which makes "VDC vs identity" part of every table.
 */
