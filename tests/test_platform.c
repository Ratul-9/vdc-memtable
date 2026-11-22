/* Platform layer: page size, map sizes, base / hugetlb mapping, populate, space with page modes. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tests.h"
#include "vdc/platform.h"
#include "vdc/vdc_space.h"

int test_platform(void)
{
    const size_t page = vdc_page_size();
    CHECK(page >= 4096 && (page & (page - 1)) == 0);

    CHECK(vdc_map_size(0, VDC_PAGES_BASE) == 0);
    CHECK(vdc_map_size(1, VDC_PAGES_BASE) == page);
    CHECK(vdc_map_size(page, VDC_PAGES_BASE) == page);
    CHECK(vdc_map_size(page + 1, VDC_PAGES_BASE) == 2 * page);
    CHECK(vdc_map_size(1, VDC_PAGES_HUGETLB) == VDC_HUGE_PAGE_SIZE);
    CHECK(vdc_map_size(SIZE_MAX, VDC_PAGES_BASE) == 0);

    CHECK(strcmp(vdc_page_mode_name(VDC_PAGES_BASE), "base") == 0);
    CHECK(strcmp(vdc_page_mode_name(VDC_PAGES_HUGETLB), "hugetlb") == 0);

    /* Base pages, lazy and populated: aligned, zeroed, writable. */
    for (int populate = 0; populate <= 1; populate++) {
        const size_t bytes = 3 * page + 1;
        unsigned char *p = vdc_map(bytes, VDC_PAGES_BASE, populate);
        CHECK(p != NULL);
        CHECK(((uintptr_t)p % page) == 0);
        for (size_t i = 0; i < vdc_map_size(bytes, VDC_PAGES_BASE); i += 997)
            CHECK(p[i] == 0);
        memset(p, 0xAB, bytes);
        CHECK(p[bytes - 1] == 0xAB);
        vdc_unmap(p, bytes, VDC_PAGES_BASE);
    }
    vdc_unmap(NULL, page, VDC_PAGES_BASE); /* NULL is ignored */

    /* Hugetlb: works only on Linux with a reserved pool, so it is optional here. */
    void *h = vdc_map(VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB, 1);
    if (!vdc_hugetlb_supported())
        CHECK(h == NULL);
    if (h != NULL) {
        CHECK(((uintptr_t)h % VDC_HUGE_PAGE_SIZE) == 0);
        ((volatile unsigned char *)h)[VDC_HUGE_PAGE_SIZE - 1] = 1;
        vdc_unmap(h, VDC_HUGE_PAGE_SIZE, VDC_PAGES_HUGETLB);
    } else {
        printf("    note: hugetlb pages not available here, hugetlb mapping not tested\n");
    }

    /* Space on populated base pages; clear empties every Node. */
    VdcSpace s;
    CHECK(vdc_space_init_ex(&s, 11, VDC_PAGES_BASE, 1) == 0);
    CHECK(s.page_mode == VDC_PAGES_BASE && s.j == 11);
    vdc_space_node(&s, 5)->flags = NODE_F_OCCUPIED;
    vdc_space_node(&s, VDC_NUM_SLOTS - 1)->flags = NODE_F_OCCUPIED;
    vdc_space_clear(&s);
    CHECK(vdc_space_node(&s, 5)->flags == 0);
    CHECK(vdc_space_node(&s, VDC_NUM_SLOTS - 1)->flags == 0);
    vdc_space_destroy(&s);

    /* Space on hugetlb pages, if the pool has 32 free pages. */
    if (vdc_space_init_ex(&s, 6, VDC_PAGES_HUGETLB, 1) == 0) {
        CHECK(((uintptr_t)s.slots % VDC_HUGE_PAGE_SIZE) == 0);
        vdc_space_node(&s, 123)->flags = NODE_F_OCCUPIED;
        CHECK(vdc_space_node(&s, 123)->flags == NODE_F_OCCUPIED);
        vdc_space_destroy(&s);
    }
    return 0;
}
