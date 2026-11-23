/*
 * platform.h - OS layer: page size, memory mapping with a page mode, prefetch.
 *
 * Page modes (decision for P14/P15):
 *   VDC_PAGES_BASE     the system's base pages (4 KiB on x86 Linux, 16 KiB on
 *                      Apple Silicon). On Linux, transparent huge pages are
 *                      disabled for the mapping (MADV_NOHUGEPAGE) so "base"
 *                      really means base pages.
 *   VDC_PAGES_HUGETLB  2 MiB pages from the reserved hugetlb pool
 *                      (MAP_HUGETLB | MAP_HUGE_2MB). Linux only. Fails if the
 *                      pool is too small: reserve it with
 *                      scripts/setup_hugepages.sh.
 *
 * populate = 1 commits every page at map time (MAP_POPULATE for hugetlb, one
 * write per page for base pages), so later operations take no page faults.
 *
 * Implemented in src/platform_linux.c (Linux) and src/platform_posix.c (other
 * POSIX systems, e.g. macOS, where hugetlb is not available).
 */
#ifndef VDC_PLATFORM_H
#define VDC_PLATFORM_H

#include <stddef.h>

#include "vdc/config.h"

typedef enum VdcPageMode {
    VDC_PAGES_BASE = 0,
    VDC_PAGES_HUGETLB = 1
} VdcPageMode;

/* Base page size of the system (sysconf). */
size_t vdc_page_size(void);

/* Bytes actually mapped for a request of `bytes` (rounded up to whole pages of
 * the mode). Returns 0 for bytes == 0 or on overflow. */
size_t vdc_map_size(size_t bytes, VdcPageMode mode);

/* Map zeroed, private, read/write memory. Returns NULL on failure (errno set;
 * ENOTSUP for hugetlb on systems without it). Page aligned; hugetlb mappings
 * are 2 MiB aligned. */
void *vdc_map(size_t bytes, VdcPageMode mode, int populate);

/*
 * Reserve `bytes` of address space on base pages WITHOUT committing memory
 * (MAP_NORESERVE, THP off). A page gets real memory only when it is first
 * written; reading an untouched page returns zeros and uses no memory.
 * Unmap with vdc_unmap(p, bytes, VDC_PAGES_BASE). NULL on failure.
 */
void *vdc_map_lazy(size_t bytes);

/* Unmap memory from vdc_map (same bytes and mode). NULL is ignored. */
void vdc_unmap(void *p, size_t bytes, VdcPageMode mode);

/* 1 if this build can use hugetlb pages at all (Linux), else 0. */
int vdc_hugetlb_supported(void);

/* "base" or "hugetlb". */
const char *vdc_page_mode_name(VdcPageMode mode);

/* Pin the calling thread to one CPU. 0 on success, -1 if unsupported (non-Linux) or failed. */
int vdc_pin_thread(int cpu);

/* Spin-wait hint (x86 PAUSE, ARM YIELD). */
static inline void vdc_cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#endif
}

/* Software prefetch of the cache line holding p, for reading / for writing. */
static inline void vdc_prefetch_r(const void *p) { __builtin_prefetch(p, 0, 3); }
static inline void vdc_prefetch_w(const void *p) { __builtin_prefetch(p, 1, 3); }

#endif /* VDC_PLATFORM_H */
