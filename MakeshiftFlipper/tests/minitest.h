#pragma once

// Minimal single-header test harness for the host-side logic tests. No
// external dependency (no Unity/CMock) so these tests build with nothing
// but a plain host C compiler -- no ESP-IDF toolchain needed.

#include <stdio.h>
#include <string.h>

static int g_mt_failures = 0;
static int g_mt_checks = 0;
static const char *g_mt_current_test = "";

#define MT_TEST(name) static void name(void)
#define MT_RUN(name) do { \
        g_mt_current_test = #name; \
        printf("  %s\n", #name); \
        name(); \
    } while (0)

#define MT_CHECK(cond) do { \
        g_mt_checks++; \
        if (!(cond)) { \
            g_mt_failures++; \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define MT_CHECK_EQ_INT(a, b) do { \
        long long _a = (long long)(a); \
        long long _b = (long long)(b); \
        g_mt_checks++; \
        if (_a != _b) { \
            g_mt_failures++; \
            printf("    FAIL %s:%d: %s == %s (%lld != %lld)\n", \
                   __FILE__, __LINE__, #a, #b, _a, _b); \
        } \
    } while (0)

#define MT_CHECK_EQ_STR(a, b) do { \
        const char *_a = (a); \
        const char *_b = (b); \
        g_mt_checks++; \
        if (strcmp(_a, _b) != 0) { \
            g_mt_failures++; \
            printf("    FAIL %s:%d: %s == %s (\"%s\" != \"%s\")\n", \
                   __FILE__, __LINE__, #a, #b, _a, _b); \
        } \
    } while (0)

#define MT_SUMMARY() \
    (printf("\n%d checks, %d failed\n", g_mt_checks, g_mt_failures), \
     g_mt_failures == 0 ? 0 : 1)
