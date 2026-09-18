#pragma once

// Host-test stub for ESP-IDF's nvs.h, backing diag.c's diag_load()/
// diag_save(). Rather than no-op stubs (which would make those two
// functions untestable), this is a tiny in-memory key-value store that
// behaves enough like real NVS for diag.c's usage: nvs_open() by
// namespace, nvs_set_blob()/nvs_get_blob() and nvs_set_i32()/nvs_get_i32()
// by key, nvs_commit() is a no-op (writes are immediate here), and state
// persists across nvs_open()/nvs_close() calls within one test process
// (simulating flash surviving a reboot) but NOT across separate test
// binaries (each test_*.c process starts with an empty store, same as a
// freshly-erased device).

#include <stdint.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 0x1102

typedef int nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

#define NVS_STUB_MAX_ENTRIES 8
#define NVS_STUB_MAX_KEY_LEN 16
#define NVS_STUB_MAX_BLOB_LEN 4096

typedef struct {
    char key[NVS_STUB_MAX_KEY_LEN];
    unsigned char blob[NVS_STUB_MAX_BLOB_LEN];
    size_t blob_len;
    int32_t i32_value;
    int has_blob;
    int has_i32;
} nvs_stub_entry_t;

static nvs_stub_entry_t g_nvs_stub_store[NVS_STUB_MAX_ENTRIES];
static int g_nvs_stub_count = 0;

// Test helper: wipes the store, simulating an erased NVS partition. Not
// part of the real esp_idf nvs.h API -- test files call this directly to
// reset state between test cases.
static inline void nvs_stub_reset(void)
{
    g_nvs_stub_count = 0;
    memset(g_nvs_stub_store, 0, sizeof(g_nvs_stub_store));
}

static inline nvs_stub_entry_t *nvs_stub_find_or_create(const char *key)
{
    for (int i = 0; i < g_nvs_stub_count; i++) {
        if (strcmp(g_nvs_stub_store[i].key, key) == 0) {
            return &g_nvs_stub_store[i];
        }
    }
    if (g_nvs_stub_count >= NVS_STUB_MAX_ENTRIES) {
        return NULL;
    }
    nvs_stub_entry_t *e = &g_nvs_stub_store[g_nvs_stub_count++];
    strncpy(e->key, key, NVS_STUB_MAX_KEY_LEN - 1);
    return e;
}

static inline nvs_stub_entry_t *nvs_stub_find(const char *key)
{
    for (int i = 0; i < g_nvs_stub_count; i++) {
        if (strcmp(g_nvs_stub_store[i].key, key) == 0) {
            return &g_nvs_stub_store[i];
        }
    }
    return NULL;
}

static inline esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    (void)namespace_name;
    (void)open_mode;
    *out_handle = 1; // single global store, namespace not modeled separately
    return ESP_OK;
}

static inline void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

static inline esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

static inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
    (void)handle;
    if (length > NVS_STUB_MAX_BLOB_LEN) {
        return ESP_FAIL;
    }
    nvs_stub_entry_t *e = nvs_stub_find_or_create(key);
    if (e == NULL) {
        return ESP_FAIL;
    }
    memcpy(e->blob, value, length);
    e->blob_len = length;
    e->has_blob = 1;
    return ESP_OK;
}

static inline esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length)
{
    (void)handle;
    nvs_stub_entry_t *e = nvs_stub_find(key);
    if (e == NULL || !e->has_blob) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    size_t copy_len = (*length < e->blob_len) ? *length : e->blob_len;
    memcpy(out_value, e->blob, copy_len);
    *length = e->blob_len;
    return ESP_OK;
}

static inline esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value)
{
    (void)handle;
    nvs_stub_entry_t *e = nvs_stub_find_or_create(key);
    if (e == NULL) {
        return ESP_FAIL;
    }
    e->i32_value = value;
    e->has_i32 = 1;
    return ESP_OK;
}

static inline esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value)
{
    (void)handle;
    nvs_stub_entry_t *e = nvs_stub_find(key);
    if (e == NULL || !e->has_i32) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out_value = e->i32_value;
    return ESP_OK;
}
