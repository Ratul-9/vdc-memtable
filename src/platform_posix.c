/* Generic POSIX implementation of platform.h (macOS etc.): no hugetlb pages. */
#ifndef __linux__

#include "vdc/platform.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
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

int vdc_hugetlb_supported(void) { return 0; }

const char *vdc_page_mode_name(VdcPageMode mode)
{
    return mode == VDC_PAGES_HUGETLB ? "hugetlb" : "base";
}

void *vdc_map(size_t bytes, VdcPageMode mode, int populate)
{
    if (mode == VDC_PAGES_HUGETLB) {
        errno = ENOTSUP;
        return NULL;
    }
    const size_t len = vdc_map_size(bytes, mode);
    if (len == 0) {
        errno = EINVAL;
        return NULL;
    }
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
    if (populate) {
        volatile unsigned char *b = (volatile unsigned char *)p;
        const size_t page = vdc_page_size();
        for (size_t off = 0; off < len; off += page)
            b[off] = 0;
    }
    return p;
}

void vdc_unmap(void *p, size_t bytes, VdcPageMode mode)
{
    if (p == NULL)
        return;
    munmap(p, vdc_map_size(bytes, mode));
}

#endif /* !__linux__ */
