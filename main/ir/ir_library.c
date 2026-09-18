#include "ir_library.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "ir_library";
#define NVS_NAMESPACE "ir_lib"
#define NVS_KEY_ENTRIES "entries"
#define NVS_KEY_COUNT "count"

static ir_library_entry_t s_entries[IR_LIBRARY_MAX_ENTRIES];
static int s_count;

void ir_library_load(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return; // nothing saved yet -- leave RAM as-is, same as diag_load()
    }

    size_t blob_len = sizeof(s_entries);
    ir_library_entry_t loaded[IR_LIBRARY_MAX_ENTRIES];
    esp_err_t err = nvs_get_blob(handle, NVS_KEY_ENTRIES, loaded, &blob_len);
    if (err != ESP_OK || blob_len != sizeof(loaded)) {
        nvs_close(handle);
        return;
    }

    int32_t count = 0;
    if (nvs_get_i32(handle, NVS_KEY_COUNT, &count) != ESP_OK ||
        count < 0 || count > IR_LIBRARY_MAX_ENTRIES) {
        nvs_close(handle);
        return;
    }

    memcpy(s_entries, loaded, sizeof(s_entries));
    s_count = (int)count;
    nvs_close(handle);
    ESP_LOGI(TAG, "loaded %d saved IR code(s) from NVS", s_count);
}

bool ir_library_save(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed, library not saved");
        return false;
    }

    bool ok = nvs_set_blob(handle, NVS_KEY_ENTRIES, s_entries, sizeof(s_entries)) == ESP_OK &&
              nvs_set_i32(handle, NVS_KEY_COUNT, (int32_t)s_count) == ESP_OK &&
              nvs_commit(handle) == ESP_OK;

    nvs_close(handle);
    if (!ok) {
        ESP_LOGW(TAG, "failed to save IR library to NVS");
    }
    return ok;
}

bool ir_library_add(const char *name, const ir_nec_frame_t *frame)
{
    if (s_count >= IR_LIBRARY_MAX_ENTRIES) {
        return false;
    }
    ir_library_entry_t *e = &s_entries[s_count];
    strncpy(e->name, name, IR_LIBRARY_NAME_MAX_LEN);
    e->name[IR_LIBRARY_NAME_MAX_LEN] = '\0';
    e->frame = *frame;
    s_count++;
    return true;
}

void ir_library_remove(int index)
{
    if (index < 0 || index >= s_count) {
        return;
    }
    for (int i = index; i < s_count - 1; i++) {
        s_entries[i] = s_entries[i + 1];
    }
    s_count--;
}

int ir_library_count(void)
{
    return s_count;
}

const ir_library_entry_t *ir_library_get(int index)
{
    if (index < 0 || index >= s_count) {
        return NULL;
    }
    return &s_entries[index];
}
