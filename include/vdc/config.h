/*
 * config.h - project-wide tunables.
 */
#ifndef VDC_CONFIG_H
#define VDC_CONFIG_H

#include <stddef.h>

/*
 * Default number of unreversed low key bits (j) used by the key -> slot map.
 * 6 = runs of 64 consecutive keys in 64 consecutive Nodes (4 KiB). To be
 * finalised from the j sweep (6, 8, 11, 15, 20).
 */
#define VDC_DEFAULT_J 6u

/*
 * Prefetch distance (in keys) of the batched get/put API: while operation i
 * runs, the Node of key i + VDC_PREFETCH_DISTANCE is being fetched.
 */
#define VDC_PREFETCH_DISTANCE 16u

/*
 * Striped locks of the concurrent memtables: one lock per 2^VDC_STRIPE_SHIFT
 * slots (6 = 64 Nodes = 4 KiB of Nodes per lock, 16384 locks of 64 B = 1 MiB).
 */
#define VDC_STRIPE_SHIFT 6u

/* Size of one huge page in the hugetlb pool. */
#define VDC_HUGE_PAGE_SIZE ((size_t)2 * 1024 * 1024)

#endif /* VDC_CONFIG_H */
