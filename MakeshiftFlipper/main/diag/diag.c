#include "diag.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "diag";
#define NVS_NAMESPACE "diag"
#define NVS_KEY_HISTORY "history"
#define NVS_KEY_COUNT "count"
#define NVS_KEY_NEXT_SLOT "next_slot"

static diag_entry_t s_history[DIAG_HISTORY_CAPACITY];
static int s_count;      // number of valid entries, caps at DIAG_HISTORY_CAPACITY
static int s_next_slot;  // ring buffer write cursor

void diag_record_error(const char *module, const char *code)
{
    diag_entry_t *e = &s_history[s_next_slot];
    strncpy(e->module, module, DIAG_MODULE_MAX_LEN);
    e->module[DIAG_MODULE_MAX_LEN] = '\0';
    strncpy(e->code, code, DIAG_CODE_MAX_LEN);
    e->code[DIAG_CODE_MAX_LEN] = '\0';
    e->timestamp_us = esp_timer_get_time();

    s_next_slot = (s_next_slot + 1) % DIAG_HISTORY_CAPACITY;
    if (s_count < DIAG_HISTORY_CAPACITY) {
        s_count++;
    }
}

void diag_clear(void)
{
    s_count = 0;
    s_next_slot = 0;
}

int diag_get_history_count(void)
{
    return s_count;
}

int diag_get_history(diag_entry_t *out_entries, int max_entries)
{
    int n = (s_count < max_entries) ? s_count : max_entries;
    // Newest-first: walk backward from the most recently written slot.
    for (int i = 0; i < n; i++) {
        int idx = (s_next_slot - 1 - i + DIAG_HISTORY_CAPACITY) % DIAG_HISTORY_CAPACITY;
        out_entries[i] = s_history[idx];
    }
    return n;
}

void diag_load(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return; // nothing saved yet (namespace doesn't exist) -- leave RAM as-is
    }

    size_t blob_len = sizeof(s_history);
    diag_entry_t loaded[DIAG_HISTORY_CAPACITY];
    esp_err_t err = nvs_get_blob(handle, NVS_KEY_HISTORY, loaded, &blob_len);
    // A size mismatch (blob_len != sizeof(loaded)) means this was saved by
    // a build with a different DIAG_HISTORY_CAPACITY/struct layout -- treat
    // that the same as "nothing saved" rather than loading a corrupt mix.
    if (err != ESP_OK || blob_len != sizeof(loaded)) {
        nvs_close(handle);
        return;
    }

    int32_t count = 0, next_slot = 0;
    if (nvs_get_i32(handle, NVS_KEY_COUNT, &count) != ESP_OK ||
        nvs_get_i32(handle, NVS_KEY_NEXT_SLOT, &next_slot) != ESP_OK ||
        count < 0 || count > DIAG_HISTORY_CAPACITY ||
        next_slot < 0 || next_slot >= DIAG_HISTORY_CAPACITY) {
        nvs_close(handle);
        return;
    }

    memcpy(s_history, loaded, sizeof(s_history));
    s_count = (int)count;
    s_next_slot = (int)next_slot;
    nvs_close(handle);
    ESP_LOGI(TAG, "loaded %d saved error(s) from NVS", s_count);
}

bool diag_save(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed, history not saved");
        return false;
    }

    bool ok = nvs_set_blob(handle, NVS_KEY_HISTORY, s_history, sizeof(s_history)) == ESP_OK &&
              nvs_set_i32(handle, NVS_KEY_COUNT, (int32_t)s_count) == ESP_OK &&
              nvs_set_i32(handle, NVS_KEY_NEXT_SLOT, (int32_t)s_next_slot) == ESP_OK &&
              nvs_commit(handle) == ESP_OK;

    nvs_close(handle);
    if (!ok) {
        ESP_LOGW(TAG, "failed to save error history to NVS");
    }
    return ok;
}
