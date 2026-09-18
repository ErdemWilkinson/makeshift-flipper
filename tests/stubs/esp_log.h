#pragma once

// Host-test stub for ESP-IDF's esp_log.h. diag.c only uses ESP_LOGI/
// ESP_LOGW for informational logging around NVS load/save -- not exercised
// by these tests' assertions, so these expand to nothing rather than
// pulling in a real logging backend.

#define ESP_LOGI(tag, fmt, ...) ((void)(tag))
#define ESP_LOGW(tag, fmt, ...) ((void)(tag))
#define ESP_LOGE(tag, fmt, ...) ((void)(tag))
