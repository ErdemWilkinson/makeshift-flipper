// Stage 2 BLE scan (C6-standalone). Fills the c6_link_bt_scan_* half of
// c6_link.h using NimBLE directly, mirroring the old c6-firmware/bt_scan.c
// but with the UART push-task replaced by an in-RAM, address-deduplicated
// list that c6_link_bt_scan_poll() snapshots -- exactly what main.c's BT
// screen already expects.
//
// Kept in its own translation unit so the NimBLE host headers don't have to
// be pulled into c6_link.c (which owns the esp_wifi side).

#include "c6_link.h"

#include <string.h>

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "radio_ble";

#define SCAN_ITVL_MS   100
#define SCAN_WINDOW_MS 100

static volatile bool s_running;
static bool s_host_synced;
static bool s_nimble_ready;
static uint8_t s_own_addr_type;
static SemaphoreHandle_t s_lock; // guards the device list below

static c6_bt_device_t s_devices[C6_BT_MAX_DEVICES];
static int s_device_count;

// Defined in c6_link.c. This firmware profile does not run promiscuous
// Wi-Fi channel hopping and BLE discovery at the same time.
bool c6_wifi_monitor_is_running(void);

// BLE names are arbitrary bytes; replace control bytes so they render
// safely on-screen. There is no longer a comma-delimited UART protocol.
static void sanitize_name(char *s)
{
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7F) {
            *s = '?';
        }
    }
}

// Pull the (complete or shortened) local name out of an advertisement's
// AD structures. Ported from the old c6-firmware extract_ble_name().
static void extract_ble_name(const uint8_t *data, uint8_t len,
                             char *out_name, size_t out_cap)
{
    out_name[0] = '\0';
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || i + 1 + field_len > len) {
            break;
        }
        uint8_t field_type = data[i + 1];
        if (field_type == 0x09 || field_type == 0x08) { // complete / short name
            size_t name_len = field_len - 1;
            if (name_len >= out_cap) {
                name_len = out_cap - 1;
            }
            memcpy(out_name, &data[i + 2], name_len);
            out_name[name_len] = '\0';
            return;
        }
        i += 1 + field_len;
    }
}

// Insert-or-update one device in the deduped list. Caller holds s_lock.
static void device_upsert(const c6_bt_device_t *dev)
{
    for (int i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].addr, dev->addr, 6) == 0) {
            s_devices[i].rssi = dev->rssi;
            if (dev->name[0] != '\0') {
                memcpy(s_devices[i].name, dev->name, sizeof(dev->name));
            }
            return;
        }
    }
    if (s_device_count < C6_BT_MAX_DEVICES) {
        s_devices[s_device_count++] = *dev;
    }
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type != BLE_GAP_EVENT_DISC || !s_running) {
        return 0;
    }
    const struct ble_gap_disc_desc *disc = &event->disc;
    c6_bt_device_t dev = {0};
    memcpy(dev.addr, disc->addr.val, 6);
    dev.rssi = (int8_t)disc->rssi;
    extract_ble_name(disc->data, disc->length_data, dev.name, sizeof(dev.name));
    sanitize_name(dev.name);

    if (s_lock != NULL && xSemaphoreTake(s_lock, 0) == pdTRUE) {
        device_upsert(&dev);
        xSemaphoreGive(s_lock);
    }
    return 0;
}

static bool start_discovery(void)
{
    struct ble_gap_disc_params params = {0};
    params.passive = 1; // listen only, don't send scan-request probes
    params.itvl = SCAN_ITVL_MS * 1000 / 625;
    params.window = SCAN_WINDOW_MS * 1000 / 625;
    params.filter_duplicates = 0; // dedup handled in device_upsert()
    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params,
                          gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_disc failed: rc=%d", rc);
        return false;
    }
    return true;
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        s_nimble_ready = false;
        s_running = false;
        ESP_LOGE(TAG, "BLE identity address unavailable: rc=%d", rc);
        return;
    }
    s_host_synced = true;
    if (s_running && !start_discovery()) {
        s_running = false;
    }
}

static void nimble_host_task(void *param)
{
    (void)param;
    nimble_port_run(); // returns only on nimble_port_stop()
    nimble_port_freertos_deinit();
}

// Brings up the NimBLE stack. Call once at startup (c6_link_init() calls
// this after Wi-Fi is up -- see the note there).
void c6_bt_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (s_lock == NULL) {
        ESP_LOGE(TAG, "BLE lock alloc failed; BT scan disabled");
        return;
    }
    if (nimble_port_init() != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed");
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(nimble_host_task);
    s_nimble_ready = true;
    ESP_LOGI(TAG, "BLE scan stack initialized");
}

bool c6_link_bt_scan_start(void)
{
    if (!s_nimble_ready || c6_wifi_monitor_is_running()) {
        return false;
    }
    if (s_running) {
        return true;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_device_count = 0;
        xSemaphoreGive(s_lock);
    }
    s_running = true;
    if (s_host_synced && !start_discovery()) {
        s_running = false;
        return false;
    }
    // else: on_sync() will start discovery once the host finishes syncing.
    return true;
}

bool c6_bt_scan_is_running(void)
{
    return s_running;
}

bool c6_link_bt_scan_stop(void)
{
    if (!s_running) {
        return true;
    }
    s_running = false;
    ble_gap_disc_cancel();
    return true;
}

int c6_link_bt_scan_poll(c6_bt_device_t *out_devices, int max_devices)
{
    if (out_devices == NULL || max_devices <= 0 || s_lock == NULL) {
        return 0;
    }
    int n = 0;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        n = s_device_count < max_devices ? s_device_count : max_devices;
        memcpy(out_devices, s_devices, n * sizeof(c6_bt_device_t));
        xSemaphoreGive(s_lock);
    }
    return n;
}
