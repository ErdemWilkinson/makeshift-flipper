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

// Defined in c6_link.c. This profile does not run local AP or promiscuous
// Wi-Fi channel hopping alongside BLE discovery.
bool c6_wifi_monitor_is_running(void);
bool c6_link_ap_is_running(void);

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

// Decode iBeacon / Eddystone from an advertisement's AD structures. Passive:
// reads only the bytes the device already broadcast, sends nothing. Fills
// out->beacon_type / out->beacon_info; leaves them at C6_BEACON_NONE / "" when
// nothing recognized is present.
//
// iBeacon: an AD of type 0xFF (Manufacturer Specific Data) whose first two
//   payload bytes are Apple's company id 0x004C (little-endian: 4C 00),
//   followed by 0x02 0x15 and then 16-byte proximity UUID + 2-byte major +
//   2-byte minor + 1-byte measured power. We summarize the UUID's last 4 bytes
//   (enough to tell beacons apart on a small screen) plus major/minor.
// Eddystone: an AD of type 0x16 (Service Data) whose first two bytes are the
//   Eddystone UUID 0xFEAA (little-endian: AA FE); the next byte is the frame
//   type (0x00 UID, 0x10 URL, 0x20 TLM, 0x30 EID). We report the frame kind.
static void decode_beacon(const uint8_t *data, uint8_t len, c6_bt_device_t *out)
{
    out->beacon_type = C6_BEACON_NONE;
    out->beacon_info[0] = '\0';
    // Some GAP disc events carry no advertisement payload (data == NULL) or a
    // zero length; walking those would dereference a null/empty buffer.
    if (data == NULL || len < 2) {
        return;
    }
    size_t i = 0;
    // i + 2 <= len guarantees data[i] (field_len) and data[i+1] (field_type)
    // are both in range before we read them.
    while (i + 2 <= len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || (size_t)(i + 1 + field_len) > len) {
            break;
        }
        uint8_t field_type = data[i + 1];
        const uint8_t *p = &data[i + 2];
        uint8_t plen = field_len - 1; // payload length after the type byte

        if (field_type == 0xFF && plen >= 24 &&
            p[0] == 0x4C && p[1] == 0x00 && p[2] == 0x02 && p[3] == 0x15) {
            // iBeacon: UUID = p[4..19], major = p[20..21], minor = p[22..23].
            // Highest index read is 23, so plen >= 24 is the exact requirement.
            uint16_t major = ((uint16_t)p[20] << 8) | p[21];
            uint16_t minor = ((uint16_t)p[22] << 8) | p[23];
            out->beacon_type = C6_BEACON_IBEACON;
            snprintf(out->beacon_info, sizeof(out->beacon_info),
                     "..%02X%02X%02X%02X M%u m%u",
                     p[16], p[17], p[18], p[19], major, minor);
            return;
        }
        if (field_type == 0x16 && plen >= 3 && p[0] == 0xAA && p[1] == 0xFE) {
            const char *kind;
            switch (p[2]) {
                case 0x00: kind = "UID"; break;
                case 0x10: kind = "URL"; break;
                case 0x20: kind = "TLM"; break;
                case 0x30: kind = "EID"; break;
                default:   kind = "?";   break;
            }
            out->beacon_type = C6_BEACON_EDDYSTONE;
            snprintf(out->beacon_info, sizeof(out->beacon_info),
                     "Eddystone-%s", kind);
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
            // Keep the most recent recognized beacon frame; don't let a later
            // advertisement without one wipe a decode we already have.
            if (dev->beacon_type != C6_BEACON_NONE) {
                s_devices[i].beacon_type = dev->beacon_type;
                memcpy(s_devices[i].beacon_info, dev->beacon_info,
                       sizeof(dev->beacon_info));
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
    decode_beacon(disc->data, disc->length_data, &dev);

    if (s_lock != NULL && xSemaphoreTake(s_lock, 0) == pdTRUE) {
        device_upsert(&dev);
        xSemaphoreGive(s_lock);
    }
    return 0;
}

static bool start_discovery(void)
{
    struct ble_gap_disc_params params = {0};
    // Active scan: send scan requests so devices reply with a scan response,
    // which is where most of them (headphones, watches, etc.) put their name.
    // A passive scan only hears the initial advertisement, so names rarely
    // appear. This is the same thing a phone does when listing BLE devices;
    // it emits small scan-request packets rather than being purely receive-only.
    params.passive = 0;
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
    if (!s_nimble_ready || c6_wifi_monitor_is_running() ||
        c6_link_ap_is_running()) {
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
