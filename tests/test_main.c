#include <stdio.h>

#include "tests.h"

typedef struct {
    const char *name;
    int (*fn)(void);
} TestCase;

static const TestCase tests[] = {
    {"vdc_bijection", test_vdc_bijection},
    {"partial_reversal", test_partial_reversal},
    {"node", test_node},
    {"vdc_space", test_vdc_space},
    {"arena", test_arena},
    {"memtable", test_memtable},
    {"bitmap", test_bitmap},
    {"platform", test_platform},
    {"concurrent", test_concurrent},
};

int main(void)
{
    const int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int failed = 0;

    for (int i = 0; i < n; i++) {
        const int rc = tests[i].fn();
        printf("[%s] %s\n", rc == 0 ? " ok " : "FAIL", tests[i].name);
        failed += (rc != 0);
    }

    printf("%d/%d tests passed\n", n - failed, n);
    return failed != 0;
}
