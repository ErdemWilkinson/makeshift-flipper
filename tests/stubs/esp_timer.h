#pragma once

// Host-test stub for ESP-IDF's esp_timer.h. diag.c's only dependency on
// ESP-IDF is esp_timer_get_time() (a boot-relative microsecond clock); this
// stub lets a test set that clock explicitly instead of reading real time,
// so ring-buffer tests can assert exact timestamp values.

#include <stdint.h>

// Test-controlled fake clock. Defined in the test file, not here, so each
// test can drive it directly.
extern int64_t g_fake_time_us;

static inline int64_t esp_timer_get_time(void)
{
    return g_fake_time_us;
}
