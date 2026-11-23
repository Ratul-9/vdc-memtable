#include "dat/dat_flat.h"

int dat_flat_init(DatFlat *f, uint64_t capacity, VdcPageMode mode)
{
    if (f == NULL)
        return MT_ERR_ARG;
    memset(f, 0, sizeof(*f));
    if (capacity == 0 || capacity > DAT_DOMAIN)
        return MT_ERR_ARG;

    const size_t bytes = (size_t)capacity * sizeof(DatNode);
    void *p = (mode == VDC_PAGES_BASE) ? vdc_map_lazy(bytes) : vdc_map(bytes, mode, 1);
    if (p == NULL)
        return MT_ERR_ALLOC;

    f->nodes = (DatNode *)p;
    f->capacity = capacity;
    f->map_bytes = bytes;
    f->mode = mode;
    f->lazy = (mode == VDC_PAGES_BASE);
    return MT_OK;
}

void dat_flat_destroy(DatFlat *f)
{
    if (f == NULL || f->nodes == NULL)
        return;
    vdc_unmap(f->nodes, f->map_bytes, f->mode);
    memset(f, 0, sizeof(*f));
}
