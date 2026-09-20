#include "rfid_library.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "rfid_library";
#define NVS_NAMESPACE "rfid_lib"
#define NVS_KEY_ENTRIES "entries"
#define NVS_KEY_COUNT "count"

static rfid_library_entry_t s_entries[RFID_LIBRARY_MAX_ENTRIES];
static int s_count;

void rfid_library_load(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return; // nothing saved yet -- leave RAM as-is, same as diag_load()
    }

    size_t blob_len = sizeof(s_entries);
    rfid_library_entry_t loaded[RFID_LIBRARY_MAX_ENTRIES];
    esp_err_t err = nvs_get_blob(handle, NVS_KEY_ENTRIES, loaded, &blob_len);
    if (err != ESP_OK || blob_len != sizeof(loaded)) {
        nvs_close(handle);
        return;
    }

    int32_t count = 0;
    if (nvs_get_i32(handle, NVS_KEY_COUNT, &count) != ESP_OK ||
        count < 0 || count > RFID_LIBRARY_MAX_ENTRIES) {
        nvs_close(handle);
        return;
    }

    memcpy(s_entries, loaded, sizeof(s_entries));
    s_count = (int)count;
    nvs_close(handle);
    ESP_LOGI(TAG, "loaded %d saved tag(s) from NVS", s_count);
}

bool rfid_library_save(void)
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
        ESP_LOGW(TAG, "failed to save RFID library to NVS");
    }
    return ok;
}

// Shared by both add_*() entry points: appends a blank entry ready for the
// caller to fill in, or returns NULL if the library is already full.
static rfid_library_entry_t *add_slot(const char *name)
{
    if (s_count >= RFID_LIBRARY_MAX_ENTRIES) {
        return NULL;
    }
    rfid_library_entry_t *e = &s_entries[s_count];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, RFID_LIBRARY_NAME_MAX_LEN);
    e->name[RFID_LIBRARY_NAME_MAX_LEN] = '\0';
    s_count++;
    return e;
}

bool rfid_library_add_125khz(const char *name, const rdm6300_id_t *id)
{
    rfid_library_entry_t *e = add_slot(name);
    if (e == NULL) {
        return false;
    }
    e->kind = RFID_LIBRARY_KIND_125KHZ;
    e->u.id_125khz = *id;
    return true;
}

bool rfid_library_add_1356mhz(const char *name, const rc522_uid_t *uid)
{
    rfid_library_entry_t *e = add_slot(name);
    if (e == NULL) {
        return false;
    }
    e->kind = RFID_LIBRARY_KIND_1356MHZ;
    e->u.uid_1356mhz = *uid;
    return true;
}

void rfid_library_remove(int index)
{
    if (index < 0 || index >= s_count) {
        return;
    }
    for (int i = index; i < s_count - 1; i++) {
        s_entries[i] = s_entries[i + 1];
    }
    s_count--;
}

int rfid_library_count(void)
{
    return s_count;
}

const rfid_library_entry_t *rfid_library_get(int index)
{
    if (index < 0 || index >= s_count) {
        return NULL;
    }
    return &s_entries[index];
}

bool rfid_library_matches_125khz(const rdm6300_id_t *id)
{
    for (int i = 0; i < s_count; i++) {
        if (s_entries[i].kind == RFID_LIBRARY_KIND_125KHZ &&
            memcmp(&s_entries[i].u.id_125khz, id, sizeof(*id)) == 0) {
            return true;
        }
    }
    return false;
}

bool rfid_library_matches_1356mhz(const rc522_uid_t *uid)
{
    for (int i = 0; i < s_count; i++) {
        if (s_entries[i].kind == RFID_LIBRARY_KIND_1356MHZ &&
            s_entries[i].u.uid_1356mhz.length == uid->length &&
            memcmp(s_entries[i].u.uid_1356mhz.bytes, uid->bytes, uid->length) == 0) {
            return true;
        }
    }
    return false;
}
