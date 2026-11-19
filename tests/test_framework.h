/*
 * Minimal single-header unit test framework (no dependencies).
 *
 * Each test file is its own executable registered with CTest:
 *
 *     static void test_something(void) { CHECK(1 + 1 == 2); }
 *
 *     int main(void)
 *     {
 *         RUN_TEST(test_something);
 *         return test_summary();
 *     }
 *
 * CHECK* macros record a failure and continue; REQUIRE aborts the current test.
 *
 * With COLONY_TEST_ONLY=name in the environment, only that test function
 * runs (the other RUN_TEST lines are skipped): tests/CMakeLists.txt registers
 * each test of the slowest files as a CTest entry of its own that way, so
 * that they run in parallel. Running nothing is then a failure (a renamed
 * test, a stale configuration).
 */
#ifndef COLONY_TEST_FRAMEWORK_H
#define COLONY_TEST_FRAMEWORK_H

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

static int test_failure_count = 0;
static int test_check_count = 0;
static int test_run_count = 0;
static const char *test_current_name = "";

/* Whether a test function should run (COLONY_TEST_ONLY, see above). */
static inline bool test_selected(const char *name)
{
    const char *only = getenv("COLONY_TEST_ONLY");
    return only == NULL || only[0] == '\0' || strcmp(only, name) == 0;
}

#define TEST_FAIL_(...)                                                                            \
    do {                                                                                           \
        test_failure_count++;                                                                      \
        fprintf(stderr, "%s:%d: [%s] ", __FILE__, __LINE__, test_current_name);                    \
        fprintf(stderr, __VA_ARGS__);                                                              \
        fputc('\n', stderr);                                                                       \
    } while (0)

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        test_check_count++;                                                                        \
        if (!(condition)) {                                                                        \
            TEST_FAIL_("CHECK failed: %s", #condition);                                            \
        }                                                                                          \
    } while (0)

/* Aborts the current test function (which must return void) on failure. */
#define REQUIRE(condition)                                                                         \
    do {                                                                                           \
        test_check_count++;                                                                        \
        if (!(condition)) {                                                                        \
            TEST_FAIL_("REQUIRE failed: %s", #condition);                                          \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                                             \
    do {                                                                                           \
        long long actual_ = (long long)(actual);                                                   \
        long long expected_ = (long long)(expected);                                               \
        test_check_count++;                                                                        \
        if (actual_ != expected_) {                                                                \
            TEST_FAIL_("%s == %lld, expected %lld", #actual, actual_, expected_);                  \
        }                                                                                          \
    } while (0)

#define CHECK_EQ_UINT(actual, expected)                                                            \
    do {                                                                                           \
        unsigned long long actual_ = (unsigned long long)(actual);                                 \
        unsigned long long expected_ = (unsigned long long)(expected);                             \
        test_check_count++;                                                                        \
        if (actual_ != expected_) {                                                                \
            TEST_FAIL_("%s == %llu (0x%llx), expected %llu (0x%llx)", #actual, actual_, actual_,   \
                       expected_, expected_);                                                      \
        }                                                                                          \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                    \
    do {                                                                                           \
        double actual_ = (double)(actual);                                                         \
        double expected_ = (double)(expected);                                                     \
        test_check_count++;                                                                        \
        if (fabs(actual_ - expected_) > (double)(tolerance)) {                                     \
            TEST_FAIL_("%s == %g, expected %g (+/- %g)", #actual, actual_, expected_,              \
                       (double)(tolerance));                                                       \
        }                                                                                          \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                                             \
    do {                                                                                           \
        const char *actual_ = (actual);                                                            \
        const char *expected_ = (expected);                                                        \
        test_check_count++;                                                                        \
        if (actual_ == NULL || expected_ == NULL || strcmp(actual_, expected_) != 0) {             \
            TEST_FAIL_("%s == \"%s\", expected \"%s\"", #actual, actual_ ? actual_ : "(null)",     \
                       expected_ ? expected_ : "(null)");                                          \
        }                                                                                          \
    } while (0)

#define RUN_TEST(function)                                                                         \
    do {                                                                                           \
        if (!test_selected(#function)) {                                                           \
            break;                                                                                 \
        }                                                                                          \
        int failures_before_ = test_failure_count;                                                 \
        test_current_name = #function;                                                             \
        test_run_count++;                                                                          \
        function();                                                                                \
        printf("%s %s\n", test_failure_count == failures_before_ ? "[ OK ]" : "[FAIL]",            \
               #function);                                                                         \
    } while (0)

/* Prints the totals; returns the process exit code. */
static inline int test_summary(void)
{
    printf("\n%d test(s), %d check(s), %d failure(s)\n", test_run_count, test_check_count,
           test_failure_count);
    if (test_run_count == 0) {
        fprintf(stderr, "no test ran (COLONY_TEST_ONLY=%s)\n", getenv("COLONY_TEST_ONLY"));
        return EXIT_FAILURE;
    }
    return test_failure_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif /* COLONY_TEST_FRAMEWORK_H */
