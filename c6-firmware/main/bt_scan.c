#include "bt_scan.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "uart_link.h"

static const char *TAG = "bt_scan";

#define DEV_QUEUE_DEPTH 32
#define SCAN_ITVL_MS 100  // how often the radio listens (NimBLE units: 0.625ms ticks, converted below)
#define SCAN_WINDOW_MS 100 // == interval, i.e. listen continuously rather than duty-cycling

typedef struct {
    uint8_t addr[6];
    char name[32];
    int8_t rssi;
} dev_entry_t;

static QueueHandle_t s_dev_queue;
static volatile bool s_running = false;
static bool s_host_synced = false;

// Sanitizes a device name for the wire, same reasoning as
// wifi_monitor.c's sanitize_ssid() -- advertised names come from
// whatever device is out there and aren't trustworthy input.
static void sanitize_name(char *name)
{
    for (char *p = name; *p != '\0'; p++) {
        if (*p == ',' || *p == '\n' || *p == '\r') {
            *p = '_';
        }
    }
}

// Pulls the Complete/Shortened Local Name AD structure out of a raw
// advertising report, if present. BLE advertising data is a sequence of
// [length][type][data...] structures; type 0x09 is "Complete Local Name",
// 0x08 is "Shortened Local Name" -- either is good enough to show.
static void extract_name(const uint8_t *data, uint8_t len, char *out_name, size_t out_cap)
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

static void uart_tx_task(void *arg)
{
    (void)arg;
    dev_entry_t entry;
    for (;;) {
        if (xQueueReceive(s_dev_queue, &entry, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        char line[96];
        snprintf(line, sizeof(line), "BTDEV:%02X%02X%02X%02X%02X%02X,%s,%d",
                 entry.addr[0], entry.addr[1], entry.addr[2],
                 entry.addr[3], entry.addr[4], entry.addr[5],
                 entry.name, entry.rssi);
        uart_link_write_line(line);
    }
}

// NimBLE GAP event callback -- runs in the NimBLE host task, not an ISR,
// so a non-blocking xQueueSend (same reasoning as wifi_monitor.c's
// promiscuous_rx_cb) is enough; a full queue just drops the report.
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    const struct ble_gap_disc_desc *disc = &event->disc;
    dev_entry_t entry = {0};
    memcpy(entry.addr, disc->addr.val, 6);
    entry.rssi = (int8_t)disc->rssi;
    extract_name(disc->data, disc->length_data, entry.name, sizeof(entry.name));
    sanitize_name(entry.name);

    xQueueSend(s_dev_queue, &entry, 0);
    return 0;
}

static void start_discovery(void)
{
    struct ble_gap_disc_params params = {0};
    // Passive scan: listen only, don't send active-scan probe requests
    // (which would otherwise ask advertisers to also reveal scan-response
    // data). Keeps this feature receive-only, same "observe, don't poke"
    // scope as Wi-Fi Monitor.
    params.passive = 1;
    params.itvl = SCAN_ITVL_MS * 1000 / 625;
    params.window = SCAN_WINDOW_MS * 1000 / 625;
    params.filter_duplicates = 0; // dedup happens on the P4 side (c6_link.c), same as Wi-Fi Monitor's PKT/AP model

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_disc failed: rc=%d", rc);
    }
}

static void on_sync(void)
{
    s_host_synced = true;
    if (s_running) {
        start_discovery();
    }
}

static void nimble_host_task(void *param)
{
    (void)param;
    nimble_port_run(); // doesn't return until nimble_port_stop()
    nimble_port_freertos_deinit();
}

void bt_scan_init(void)
{
    s_dev_queue = xQueueCreate(DEV_QUEUE_DEPTH, sizeof(dev_entry_t));
    xTaskCreate(uart_tx_task, "bt_scan_tx", 3072, NULL, tskIDLE_PRIORITY + 1, NULL);

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return;
    }

    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE scan stack initialized");
}

bool bt_scan_start(void)
{
    if (s_running) {
        return true;
    }
    s_running = true;
    if (s_host_synced) {
        start_discovery();
    }
    // If the host hasn't synced yet, on_sync() will start discovery once
    // it does -- there's a short window at boot before NimBLE's sync
    // callback fires, and bt_scan_start() could in principle be called
    // (from the P4's "BTSCAN" command) before that.
    return true;
}

bool bt_scan_stop(void)
{
    if (!s_running) {
        return true;
    }
    s_running = false;
    ble_gap_disc_cancel();
    return true;
}
