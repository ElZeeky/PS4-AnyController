/*
 * tests/framework.h - tiny assertion framework.
 *
 * No external dependency: the PS4 build machine and this PC should not need a
 * test runner installed just to compile and run the suite.
 */
#ifndef XCP_TEST_FRAMEWORK_H
#define XCP_TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

extern int         g_tests_run;
extern int         g_tests_failed;
extern int         g_checks_run;
extern int         g_checks_failed;
extern const char *g_current_test;

typedef void (*xcp_test_fn)(void);

typedef struct { const char *name; xcp_test_fn fn; } xcp_test_case;

/* Register a test; ids are unique, the runner looks them up by name. */
void xcp_test_register(const char *name, xcp_test_fn fn);

void xcp_test_fail(const char *file, int line, const char *fmt, ...);
void xcp_test_start(const char *name);
void xcp_test_end(void);

#define XCP_TEST(name) static void name(void)

#define XCP_RUN(name) xcp_run_one(#name, name)

void xcp_run_one(const char *name, xcp_test_fn fn);

#define XCP_CHECK(cond)                                                       \
    do {                                                                      \
        g_checks_run++;                                                       \
        if (!(cond)) {                                                        \
            xcp_test_fail(__FILE__, __LINE__, "expected true: %s", #cond);     \
        }                                                                     \
    } while (0)

#define XCP_CHECK_FALSE(cond)                                                 \
    do {                                                                      \
        g_checks_run++;                                                       \
        if ((cond)) {                                                         \
            xcp_test_fail(__FILE__, __LINE__, "expected false: %s", #cond);    \
        }                                                                     \
    } while (0)

#define XCP_CHECK_EQ_INT(actual, expected)                                    \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        g_checks_run++;                                                       \
        if (a_ != e_) {                                                       \
            xcp_test_fail(__FILE__, __LINE__,                                 \
                          "%s == %s (got %lld, want %lld)",                   \
                          #actual, #expected, a_, e_);                        \
        }                                                                     \
    } while (0)

#define XCP_CHECK_EQ_HEX(actual, expected)                                    \
    do {                                                                      \
        unsigned long long a_ = (unsigned long long)(actual);                 \
        unsigned long long e_ = (unsigned long long)(expected);               \
        g_checks_run++;                                                       \
        if (a_ != e_) {                                                       \
            xcp_test_fail(__FILE__, __LINE__,                                 \
                          "%s == %s (got 0x%llX, want 0x%llX)",               \
                          #actual, #expected, a_, e_);                        \
        }                                                                     \
    } while (0)

#define XCP_CHECK_MEM(actual, expected, len)                                  \
    do {                                                                      \
        g_checks_run++;                                                       \
        if (memcmp((actual), (expected), (len)) != 0) {                       \
            xcp_test_fail(__FILE__, __LINE__, "memory differs: %s vs %s",      \
                          #actual, #expected);                                \
        }                                                                     \
    } while (0)

#endif /* XCP_TEST_FRAMEWORK_H */
