// Stage 2 BLE scan (C6-standalone) - passive advertisement discovery
#include "c6_link.h"

#include <string.h>

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "esp_random.h"

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
static SemaphoreHandle_t s_lock;

static c6_bt_device_t s_devices[C6_BT_MAX_DEVICES];
static int s_device_count;

bool c6_wifi_monitor_is_running(void);
bool c6_link_ap_is_running(void);

// Fonksiyon prototipi (Olay döngüsü için ileri bildirim)
static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void sanitize_name(char *s)
{
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7F) {
            *s = '?';
        }
    }
}

static void extract_ble_name(const uint8_t *data, uint8_t len, char *out_name, size_t out_cap)
{
    out_name[0] = '\0';
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || i + 1 + field_len > len) {
            break;
        }
        uint8_t field_type = data[i + 1];
        if (field_type == 0x09 || field_type == 0x08) {
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

static void decode_beacon(const uint8_t *data, uint8_t len, c6_bt_device_t *out)
{
    out->beacon_type = C6_BEACON_NONE;
    out->beacon_info[0] = '\0';
    if (data == NULL || len < 2) {
        return;
    }
    size_t i = 0;
    while (i + 2 <= len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || (size_t)(i + 1 + field_len) > len) {
            break;
        }
        uint8_t field_type = data[i + 1];
        const uint8_t *p = &data[i + 2];
        uint8_t plen = field_len - 1;

        if (field_type == 0xFF && plen >= 24 &&
            p[0] == 0x4C && p[1] == 0x00 && p[2] == 0x02 && p[3] == 0x15) {
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
            snprintf(out->beacon_info, sizeof(out->beacon_info), "Eddystone-%s", kind);
            return;
        }
        i += 1 + field_len;
    }
}

static void classify_ble_kind(const uint8_t *data, uint8_t len, c6_bt_device_t *out)
{
    out->kind[0] = '\0';
    const char *guess = NULL;
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || i + 1 + field_len > len) {
            break;
        }
        uint8_t ft = data[i + 1];
        const uint8_t *p = &data[i + 2];
        uint8_t plen = field_len - 1;

        if ((ft == 0x02 || ft == 0x03)) {
            for (int u = 0; u + 1 < plen; u += 2) {
                uint16_t uuid = (uint16_t)p[u] | ((uint16_t)p[u + 1] << 8);
                switch (uuid) {
                    case 0x1812: guess = "HID"; break;
                    case 0x180D: guess = "Kalp"; break;
                    case 0x1108: case 0x110B: case 0x111E: guess = "Kulaklik"; break;
                    case 0x1816: guess = "Bisiklet"; break;
                    case 0xFD6F: guess = "Temas"; break;
                    default: break;
                }
                if (guess) break;
            }
        } else if (ft == 0x19 && plen >= 2) {
            uint16_t app = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            uint16_t cat = app >> 6;
            switch (cat) {
                case 0x005: guess = "Saat"; break;
                case 0x00F: guess = "Bantli"; break;
                case 0x00D: case 0x226: guess = "Kulaklik"; break;
                case 0x00C: guess = "HID"; break;
                case 0x011: guess = "Termo"; break;
                default: break;
            }
        }
        i += 1 + field_len;
    }
    if (guess) {
        strncpy(out->kind, guess, C6_BT_KIND_MAX_LEN);
        out->kind[C6_BT_KIND_MAX_LEN] = '\0';
    }
}

// Walk the AD structures and return the manufacturer "company id" from the
// first Manufacturer Specific Data field (AD type 0xFF): its first two payload
// bytes are the company id, little-endian. Returns 0xFFFF if none is present.
// Purely passive parsing of bytes the device already broadcast.
static uint16_t extract_company_id(const uint8_t *data, uint8_t len)
{
    if (data == NULL || len < 2) {
        return 0xFFFF;
    }
    size_t i = 0;
    while (i + 2 <= len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || (size_t)(i + 1 + field_len) > len) {
            break;
        }
        uint8_t field_type = data[i + 1];
        if (field_type == 0xFF && field_len >= 3) {
            // payload starts at i+2; first two bytes = company id (LE)
            return (uint16_t)data[i + 2] | ((uint16_t)data[i + 3] << 8);
        }
        i += 1 + field_len;
    }
    return 0xFFFF;
}

static void device_upsert(const c6_bt_device_t *dev)
{
    for (int i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].addr, dev->addr, 6) == 0) {
            s_devices[i].rssi = dev->rssi;
            s_devices[i].seen_seq++;
            s_devices[i].addr_type = dev->addr_type;
            // Keep a known company id; don't let a later packet without
            // manufacturer data (0xFFFF) wipe one we already decoded.
            if (dev->company_id != 0xFFFF) {
                s_devices[i].company_id = dev->company_id;
            }
            if (dev->name[0] != '\0') {
                memcpy(s_devices[i].name, dev->name, sizeof(dev->name));
            }
            if (dev->beacon_type != C6_BEACON_NONE) {
                s_devices[i].beacon_type = dev->beacon_type;
                memcpy(s_devices[i].beacon_info, dev->beacon_info, sizeof(dev->beacon_info));
            }
            if (dev->kind[0] != '\0') {
                memcpy(s_devices[i].kind, dev->kind, sizeof(dev->kind));
            }
            return;
        }
    }
    if (s_device_count < C6_BT_MAX_DEVICES) {
        s_devices[s_device_count] = *dev;
        s_devices[s_device_count].seen_seq = 1;
        s_device_count++;
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
    dev.addr_type = disc->addr.type; // 0/1 public/static, 2/3 random (privacy)
    extract_ble_name(disc->data, disc->length_data, dev.name, sizeof(dev.name));
    sanitize_name(dev.name);
    decode_beacon(disc->data, disc->length_data, &dev);
    classify_ble_kind(disc->data, disc->length_data, &dev);
    dev.company_id = extract_company_id(disc->data, disc->length_data);

    if (s_lock != NULL && xSemaphoreTake(s_lock, 0) == pdTRUE) {
        device_upsert(&dev);
        xSemaphoreGive(s_lock);
    }
    return 0;
}

static bool start_discovery(void)
{
    struct ble_gap_disc_params params = {0};
    params.passive = 0;
    params.itvl = SCAN_ITVL_MS * 1000 / 625;
    params.window = SCAN_WINDOW_MS * 1000 / 625;
    params.filter_duplicates = 0;
    
    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    return (rc == 0);
}

static void on_sync(void)
{
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        s_nimble_ready = false;
        s_running = false;
        return;
    }
    s_host_synced = true;
    if (s_running) {
        start_discovery();
    }
}

static void nimble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void c6_bt_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (nimble_port_init() != ESP_OK) {
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(nimble_host_task);
    s_nimble_ready = true;
}

bool c6_link_bt_scan_start(void)
{
    if (!s_nimble_ready || c6_wifi_monitor_is_running() || c6_link_ap_is_running()) {
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
    if (s_host_synced) {
        start_discovery();
    }
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