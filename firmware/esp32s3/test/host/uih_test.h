/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Minimal host unit test helpers.
 *
 * Each test file is one executable: RUN(test_fn) runs a test, and
 * test_main_result() returns the process exit code.
 */

#ifndef UIH_TEST_H_
#define UIH_TEST_H_

#include <stdio.h>
#include <string.h>

static int uih_test_failures = 0;
static int uih_test_count = 0;
static int uih_test_current_failed = 0;

#define FAIL_(fmt, ...) do { \
    printf("  %s:%d: " fmt "\n", __FILE__, __LINE__, __VA_ARGS__); \
    uih_test_current_failed = 1; \
    return; \
} while (0)

#define ASSERT_TRUE(x) do { if (!(x)) { FAIL_("expected true: %s", #x); } } while (0)
#define ASSERT_FALSE(x) do { if (x) { FAIL_("expected false: %s", #x); } } while (0)
#define ASSERT_EQ(expect, actual) do { \
    long long e_ = (long long) (expect), a_ = (long long) (actual); \
    if (e_ != a_) { FAIL_("%s: expected %lld, got %lld", #actual, e_, a_); } \
} while (0)
#define ASSERT_STR_EQ(expect, actual) do { \
    const char * e_ = (expect); const char * a_ = (actual); \
    if (strcmp(e_, a_)) { FAIL_("%s:\n    expected: %s\n    actual:   %s", #actual, e_, a_); } \
} while (0)

#define RUN(fn) do { \
    uih_test_current_failed = 0; \
    ++uih_test_count; \
    fn(); \
    if (uih_test_current_failed) { ++uih_test_failures; printf("FAIL %s\n", #fn); } \
} while (0)

static inline int test_main_result(const char * name) {
    printf("%s: %d tests, %d failed\n", name, uih_test_count, uih_test_failures);
    return uih_test_failures ? 1 : 0;
}

#endif  /* UIH_TEST_H_ */
