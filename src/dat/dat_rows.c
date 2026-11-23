#include "dat/dat_rows.h"

#include <string.h>

int dat_rows_init(DatRows *r, unsigned domain_bits, unsigned row_bits, uint64_t max_rows, VdcPageMode mode)
{
    if (r == NULL)
        return MT_ERR_ARG;
    memset(r, 0, sizeof(*r));
    if (domain_bits < 1 || domain_bits > 30 || row_bits > domain_bits || row_bits > 24 || max_rows == 0)
        return MT_ERR_ARG;

    r->domain_bits = domain_bits;
    r->row_bits = row_bits;
    r->row_mask = (uint32_t)(((uint64_t)1 << row_bits) - 1);
    r->table_rows = (uint64_t)1 << (domain_bits - row_bits);
    if (max_rows > r->table_rows)
        max_rows = r->table_rows;
    r->mode = mode;

    r->table_bytes = (size_t)r->table_rows * sizeof(uint32_t);
    r->table = vdc_map(r->table_bytes, mode, 1);
    if (r->table == NULL) {
        memset(r, 0, sizeof(*r));
        return MT_ERR_ALLOC;
    }

    r->arena_rows = max_rows;
    r->arena_bytes = (size_t)max_rows * dat_rows_row_bytes(r);
    r->arena = vdc_map(r->arena_bytes, mode, 1);
    if (r->arena == NULL) {
        vdc_unmap(r->table, r->table_bytes, mode);
        memset(r, 0, sizeof(*r));
        return MT_ERR_ALLOC;
    }
    return MT_OK;
}

void dat_rows_destroy(DatRows *r)
{
    if (r == NULL || r->table == NULL)
        return;
    vdc_unmap(r->arena, r->arena_bytes, r->mode);
    vdc_unmap(r->table, r->table_bytes, r->mode);
    memset(r, 0, sizeof(*r));
}

void dat_rows_clear(DatRows *r)
{
    if (r == NULL || r->table == NULL)
        return;
    memset(r->arena, 0, (size_t)r->used_rows * dat_rows_row_bytes(r));
    memset(r->table, 0, r->table_bytes);
    r->used_rows = 0;
}
