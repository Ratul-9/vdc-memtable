/* Linux implementation of platform.h (hugetlb, MAP_POPULATE, MADV_NOHUGEPAGE). */
#ifdef __linux__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "vdc/platform.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000
#endif
#ifndef MAP_HUGE_SHIFT
#define MAP_HUGE_SHIFT 26
#endif
#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21 << MAP_HUGE_SHIFT)
#endif

size_t vdc_page_size(void)
{
    const long p = sysconf(_SC_PAGESIZE);
    return p > 0 ? (size_t)p : 4096u;
}

size_t vdc_map_size(size_t bytes, VdcPageMode mode)
{
    const size_t unit = (mode == VDC_PAGES_HUGETLB) ? VDC_HUGE_PAGE_SIZE : vdc_page_size();
    if (bytes == 0 || bytes > SIZE_MAX - (unit - 1))
        return 0;
    return (bytes + unit - 1) / unit * unit;
}

int vdc_hugetlb_supported(void) { return 1; }

const char *vdc_page_mode_name(VdcPageMode mode)
{
    return mode == VDC_PAGES_HUGETLB ? "hugetlb" : "base";
}

/* Write one byte per page so every page is committed now. */
static void touch_pages(void *p, size_t len, size_t page)
{
    volatile unsigned char *b = (volatile unsigned char *)p;
    for (size_t off = 0; off < len; off += page)
        b[off] = 0;
}

void *vdc_map(size_t bytes, VdcPageMode mode, int populate)
{
    const size_t len = vdc_map_size(bytes, mode);
    if (len == 0) {
        errno = EINVAL;
        return NULL;
    }

    if (mode == VDC_PAGES_HUGETLB) {
        int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB;
        if (populate)
            flags |= MAP_POPULATE;
        void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, flags, -1, 0);
        return p == MAP_FAILED ? NULL : p;
    }

    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
#ifdef MADV_NOHUGEPAGE
    /* Keep base mode on base pages even when THP is set to "always". */
    (void)madvise(p, len, MADV_NOHUGEPAGE);
#endif
    if (populate)
        touch_pages(p, len, vdc_page_size());
    return p;
}

void vdc_unmap(void *p, size_t bytes, VdcPageMode mode)
{
    if (p == NULL)
        return;
    munmap(p, vdc_map_size(bytes, mode));
}

#endif /* __linux__ */
