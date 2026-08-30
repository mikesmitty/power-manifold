#pragma once

// Minimal test harness: no dependencies, assertion failures name the file and
// line, first failed assertion aborts the test function (via return).

#include <stdio.h>

extern int mt_failures;
extern int mt_asserts;

#define MT_FAIL(fmt, ...)                                                   \
    do {                                                                    \
        printf("    FAIL %s:%d: " fmt "\n", __FILE__, __LINE__,             \
               ##__VA_ARGS__);                                              \
        mt_failures++;                                                      \
    } while (0)

#define MT_ASSERT(cond)                                                     \
    do {                                                                    \
        mt_asserts++;                                                       \
        if (!(cond)) {                                                      \
            MT_FAIL("%s", #cond);                                           \
            return;                                                         \
        }                                                                   \
    } while (0)

#define MT_ASSERT_EQ(a, b)                                                  \
    do {                                                                    \
        mt_asserts++;                                                       \
        long long mt_a = (long long)(a), mt_b = (long long)(b);             \
        if (mt_a != mt_b) {                                                 \
            MT_FAIL("%s == %s (%lld != %lld)", #a, #b, mt_a, mt_b);         \
            return;                                                         \
        }                                                                   \
    } while (0)

void mt_run(const char *name, void (*fn)(void));
int mt_summary(void); // prints totals; returns process exit code
