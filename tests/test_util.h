/*
 * test_util.h -- Zero-dependency C89 assertion runner for host unit tests.
 *
 * No framework, no build deps: each test_*.c is its own executable with a
 * main() that calls CHECK/CHECK_EQ and returns TEST_RESULT(). Keeps the test
 * build as portable as the code under test (native gcc, -std=c89). See
 * notes/deepening-and-testability.md.
 */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond, msg) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            g_fails++; \
            printf("  FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); \
        } \
    } while (0)

#define CHECK_EQ(got, want, msg) \
    do { \
        long g_ = (long)(got); \
        long w_ = (long)(want); \
        g_checks++; \
        if (g_ != w_) { \
            g_fails++; \
            printf("  FAIL: %s: got %ld (0x%lX) want %ld (0x%lX)  (%s:%d)\n", \
                   (msg), g_, g_, w_, w_, __FILE__, __LINE__); \
        } \
    } while (0)

#define TEST_RESULT() \
    (printf("  %d checks, %d failed\n", g_checks, g_fails), g_fails == 0 ? 0 : 1)

#endif /* TEST_UTIL_H */
