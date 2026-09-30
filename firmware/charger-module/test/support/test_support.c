#include <stdio.h>

#include "microtest.h"

int mt_failures;
int mt_asserts;
static int mt_tests;
static int mt_failed_tests;

void mt_run(const char *name, void (*fn)(void)) {
    int before = mt_failures;
    printf("  %s\n", name);
    fn();
    mt_tests++;
    if (mt_failures != before) mt_failed_tests++;
}

int mt_summary(void) {
    printf("%d tests, %d assertions, %d failures\n", mt_tests, mt_asserts,
           mt_failures);
    return mt_failed_tests ? 1 : 0;
}
