/*
 * tests/run_tests.c - test runner.
 *
 * Each test_*.c file exposes a xcp_register_<area>_tests() function; keep the
 * list below in sync when adding one.
 */
#include "framework.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int         g_tests_run     = 0;
int         g_tests_failed  = 0;
int         g_checks_run    = 0;
int         g_checks_failed = 0;
const char *g_current_test  = "";
static int  g_test_checks_failed_before = 0;

void xcp_test_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    g_checks_failed++;
    printf("    \033[31mFAIL\033[0m %s:%d  ", file, line);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

void xcp_run_one(const char *name, xcp_test_fn fn)
{
    g_current_test = name;
    g_test_checks_failed_before = g_checks_failed;

    printf("  %s\n", name);
    fn();

    g_tests_run++;
    if (g_checks_failed > g_test_checks_failed_before) {
        g_tests_failed++;
    }
}

void xcp_register_translate_tests(void);
void xcp_register_pad_state_tests(void);
void xcp_register_usb_mock_tests(void);
void xcp_register_router_tests(void);
void xcp_register_klog_line_tests(void);

int main(void)
{
    printf("\033[1m== xcp unit tests\033[0m\n");

    printf("\n[translate]\n");
    xcp_register_translate_tests();
    printf("\n[pad_state]\n");
    xcp_register_pad_state_tests();
    printf("\n[usb_mock]\n");
    xcp_register_usb_mock_tests();
    printf("\n[router]\n");
    xcp_register_router_tests();
    printf("\n[klog_line]\n");
    xcp_register_klog_line_tests();

    printf("\n\033[1m== %d tests, %d checks, %d failed\033[0m\n",
           g_tests_run, g_checks_run, g_checks_failed);

    if (g_checks_failed != 0) {
        printf("\033[31mFAILED\033[0m\n");
        return 1;
    }
    printf("\033[32mOK\033[0m\n");
    return 0;
}
