#include "vdc/vdc_space.h"

#include <string.h>

int vdc_space_init_ex(VdcSpace *space, unsigned j, VdcPageMode mode, int populate)
{
    if (space == NULL)
        return -1;

    space->slots = NULL;
    space->j = 0;
    space->page_mode = VDC_PAGES_BASE;

    if (j > VDC_KEY_BITS)
        return -1;

    void *p = vdc_map(VDC_SPACE_BYTES, mode, populate);
    if (p == NULL)
        return -1;

    space->slots = (Node *)p;
    space->j = j;
    space->page_mode = mode;
    return 0;
}

int vdc_space_init(VdcSpace *space, unsigned j)
{
    return vdc_space_init_ex(space, j, VDC_PAGES_BASE, 0);
}

void vdc_space_clear(VdcSpace *space)
{
    if (space != NULL && space->slots != NULL)
        memset(space->slots, 0, VDC_SPACE_BYTES);
}

void vdc_space_destroy(VdcSpace *space)
{
    if (space == NULL || space->slots == NULL)
        return;
    vdc_unmap(space->slots, VDC_SPACE_BYTES, space->page_mode);
    space->slots = NULL;
}
