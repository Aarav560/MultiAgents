/* test.h - minimal unit-test harness shared by every tests/test_*.c file.
 *
 *   #include "test.h"
 *   static void test_something(void) { CHECK(1 + 1 == 2); CHECK_NEAR(0.1 + 0.2, 0.3, 1e-12); }
 *   int main(void) { RUN(test_something); return TEST_SUMMARY(); }
 */
#ifndef ORBIT_TEST_H
#define ORBIT_TEST_H

#include <math.h>
#include <stdio.h>

static int test_failures = 0;
static int test_checks = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        test_checks++;                                                                  \
        if (!(cond)) {                                                                  \
            test_failures++;                                                            \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                               \
    } while (0)

/* Passes when |a - b| <= tol. */
#define CHECK_NEAR(a, b, tol)                                                           \
    do {                                                                                \
        double test_a_ = (a), test_b_ = (b);                                            \
        test_checks++;                                                                  \
        if (!(fabs(test_a_ - test_b_) <= (tol))) {                                      \
            test_failures++;                                                            \
            fprintf(stderr, "  FAIL %s:%d: %s = %.12g, expected %.12g (tol %g)\n",       \
                    __FILE__, __LINE__, #a, test_a_, test_b_, (double)(tol));           \
        }                                                                               \
    } while (0)

/* Passes when |a - b| <= rel * max(|a|, |b|). */
#define CHECK_REL(a, b, rel)                                                            \
    do {                                                                                \
        double test_a_ = (a), test_b_ = (b);                                            \
        double test_m_ = fabs(test_a_) > fabs(test_b_) ? fabs(test_a_) : fabs(test_b_); \
        test_checks++;                                                                  \
        if (!(fabs(test_a_ - test_b_) <= (rel) * test_m_)) {                            \
            test_failures++;                                                            \
            fprintf(stderr, "  FAIL %s:%d: %s = %.12g, expected %.12g (rel %g)\n",       \
                    __FILE__, __LINE__, #a, test_a_, test_b_, (double)(rel));           \
        }                                                                               \
    } while (0)

#define RUN(fn)                                                                         \
    do {                                                                                \
        int test_before_ = test_failures;                                               \
        fn();                                                                           \
        fprintf(stderr, "%s %s\n", test_failures == test_before_ ? "ok  " : "FAIL", #fn); \
    } while (0)

#define TEST_SUMMARY()                                                                  \
    (fprintf(stderr, "%s: %d checks, %d failures\n", __FILE__, test_checks, test_failures), \
     test_failures != 0)

#endif
