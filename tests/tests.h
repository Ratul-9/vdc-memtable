/*
 * tests.h - minimal test helpers. Each test is a function returning 0 on pass
 * and 1 on failure; test_main.c runs them all.
 */
#ifndef VDC_TESTS_H
#define VDC_TESTS_H

#include <stdio.h>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                            \
        }                                                                        \
    } while (0)

int test_vdc_bijection(void);
int test_partial_reversal(void);
int test_node(void);
int test_vdc_space(void);
int test_arena(void);
int test_memtable(void);
int test_bitmap(void);
int test_platform(void);
int test_concurrent(void);
int test_dat(void);
int test_dat_conc(void);

#endif /* VDC_TESTS_H */
