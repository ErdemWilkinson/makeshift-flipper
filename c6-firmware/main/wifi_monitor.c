#include "wifi_monitor.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "text_sanitize.h"
#include "uart_link.h"

static const char *TAG = "wifi_monitor";

#define CHANNEL_COUNT 13
#define CHANNEL_HOP_MS 400
#define PKT_QUEUE_DEPTH 32

typedef struct {
    uint8_t bssid[6];
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    char sec[8];
} pkt_entry_t;

static QueueHandle_t s_pkt_queue;
static TaskHandle_t s_tx_task_handle;
static TimerHandle_t s_hop_timer;
static SemaphoreHandle_t s_tx_mutex;
static volatile bool s_running = false;
static int s_current_channel = 1;

// Deduplication cache to prevent flooding UART with identical BSSIDs on the same channel hop
#define BSSID_CACHE_SIZE 16
typedef struct {
    uint8_t bssid[6];
    TickType_t last_seen_tick;
} bssid_cache_t;

static bssid_cache_t s_bssid_cache[BSSID_CACHE_SIZE];
static int s_cache_idx = 0;

static bool is_recently_sent(const uint8_t *bssid)
{
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < BSSID_CACHE_SIZE; i++) {
        if (memcmp(s_bssid_cache[i].bssid, bssid, 6) == 0) {
            if ((now - s_bssid_cache[i].last_seen_tick) < pdMS_TO_TICKS(500)) {
                return true;
            }
            s_bssid_cache[i].last_seen_tick = now;
            return false;
        }
    }
    memcpy(s_bssid_cache[s_cache_idx].bssid, bssid, 6);
    s_bssid_cache[s_cache_idx].last_seen_tick = now;
    s_cache_idx = (s_cache_idx + 1) % BSSID_CACHE_SIZE;
    return false;
}

// 802.11 management frame subtypes
#define FRAME_SUBTYPE_BEACON         0x80
#define FRAME_SUBTYPE_PROBE_RESPONSE 0x50
#define FRAME_SUBTYPE_MASK           0xF0

static const char *parse_security_mode(const uint8_t *payload, int len)
{
    if (len < 36) {
        return "UNKNOWN";
    }
    // Capability info is at offset 34 (24-byte 802.11 header + 8-byte timestamp + 2-byte interval = 34)
    uint16_t cap_info = (uint16_t)payload[34] | ((uint16_t)payload[35] << 8);
    bool privacy = (cap_info & 0x0010) != 0;

    if (!privacy) {
        return "OPEN";
    }

    bool rsn_found = false;
    bool wpa_found = false;
    bool wpa3_found = false;

    // Loop Information Elements starting right after beacon fixed fields (offset 36)
    int ie_offset = 36;
    while (ie_offset + 2 <= len) {
        uint8_t id = payload[ie_offset];
        uint8_t ie_len = payload[ie_offset + 1];
        if (ie_offset + 2 + ie_len > len) {
            break;
        }

        if (id == 0x30) { // RSN IE (WPA2 / WPA3)
            rsn_found = true;
            const uint8_t *ie_data = &payload[ie_offset + 2];
            if (ie_len >= 10) {
                uint16_t pairwise_count = (uint16_t)ie_data[6] | ((uint16_t)ie_data[7] << 8);
                int akm_offset = 8 + 4 * pairwise_count;
                if (akm_offset + 2 <= ie_len) {
                    uint16_t akm_count = (uint16_t)ie_data[akm_offset] | ((uint16_t)ie_data[akm_offset + 1] << 8);
                    int akm_list_offset = akm_offset + 2;
                    for (int k = 0; k < akm_count && akm_list_offset + 4 <= ie_len; k++) {
                        if (ie_data[akm_list_offset] == 0x00 &&
                            ie_data[akm_list_offset + 1] == 0x0F &&
                            ie_data[akm_list_offset + 2] == 0xAC) {
                            uint8_t akm_type = ie_data[akm_list_offset + 3];
                            if (akm_type == 8 || akm_type == 24) { // SAE / SAE-EXT (WPA3)
                                wpa3_found = true;
                            }
                        }
                        akm_list_offset += 4;
                    }
                }
            }
        } else if (id == 0xDD) { // Vendor Specific IE (WPA1)
            const uint8_t *ie_data = &payload[ie_offset + 2];
            if (ie_len >= 4 && ie_data[0] == 0x00 && ie_data[1] == 0x50 && ie_data[2] == 0xF2 && ie_data[3] == 0x01) {
                wpa_found = true;
            }
        }
        ie_offset += 2 + ie_len;
    }

    if (wpa3_found) {
        return "WPA3";
    } else if (rsn_found) {
        return "WPA2";
    } else if (wpa_found) {
        return "WPA";
    }
    return "WEP";
}

static void promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT) {
        return;
    }
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *payload = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;

    if (len < 36) { // fixed 802.11 mgmt header (24) + beacon fixed fields (12)
        return;
    }

    uint8_t subtype = payload[0] & FRAME_SUBTYPE_MASK;
    if (subtype != FRAME_SUBTYPE_BEACON && subtype != FRAME_SUBTYPE_PROBE_RESPONSE) {
        return;
    }

    pkt_entry_t entry = {0};
    memcpy(entry.bssid, &payload[10], 6); // addr2 (transmitter/BSSID)

    if (is_recently_sent(entry.bssid)) {
        return;
    }

    entry.rssi = pkt->rx_ctrl.rssi;
    entry.channel = pkt->rx_ctrl.channel;

    int ie_offset = 36;
    if (ie_offset + 2 > len || payload[ie_offset] != 0x00) {
        return;
    }
    int ssid_len = payload[ie_offset + 1];
    if (ssid_len > 32 || ie_offset + 2 + ssid_len > len) {
        return;
    }
    memcpy(entry.ssid, &payload[ie_offset + 2], ssid_len);
    entry.ssid[ssid_len] = '\0';
    sanitize_wire_text(entry.ssid);

    const char *sec = parse_security_mode(payload, len);
    strncpy(entry.sec, sec, sizeof(entry.sec) - 1);
    entry.sec[sizeof(entry.sec) - 1] = '\0';

    if (s_running && s_pkt_queue != NULL) {
        xQueueSend(s_pkt_queue, &entry, 0);
    }
}

static void uart_tx_task(void *arg)
{
    (void)arg;
    pkt_entry_t entry;
    for (;;) {
        if (xQueueReceive(s_pkt_queue, &entry, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
        if (s_running) {
            char line[96];
            snprintf(line, sizeof(line), "PKT:%02X%02X%02X%02X%02X%02X,%s,%d,%d,%s",
                     entry.bssid[0], entry.bssid[1], entry.bssid[2],
                     entry.bssid[3], entry.bssid[4], entry.bssid[5],
                     entry.ssid, entry.rssi, entry.channel, entry.sec);
            uart_link_write_line(line);
        }
        xSemaphoreGive(s_tx_mutex);
    }
}

static void hop_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    s_current_channel = (s_current_channel % CHANNEL_COUNT) + 1;
    esp_wifi_set_channel(s_current_channel, WIFI_SECOND_CHAN_NONE);
}

void wifi_monitor_init(void)
{
    s_pkt_queue = xQueueCreate(PKT_QUEUE_DEPTH, sizeof(pkt_entry_t));
    s_tx_mutex = xSemaphoreCreateMutex();
    s_hop_timer = xTimerCreate("wifi_mon_hop", pdMS_TO_TICKS(CHANNEL_HOP_MS),
                                pdTRUE, NULL, hop_timer_cb);
    if (s_pkt_queue == NULL || s_tx_mutex == NULL || s_hop_timer == NULL) {
        ESP_LOGE(TAG, "Wi-Fi Monitor resources unavailable; feature disabled");
        return;
    }
    // Same boot-time, one-shot, no-retry-path situation as bt_scan_init()'s
    // uart_tx_task -- log loudly on failure instead of leaving a device that
    // silently never reports any AP it sees.
    if (xTaskCreate(uart_tx_task, "wifi_mon_tx", 3072, NULL, tskIDLE_PRIORITY + 1,
                     &s_tx_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(wifi_mon_tx) failed -- WiFi Monitor results won't reach the P4");
    }
}

bool wifi_monitor_start(void)
{
    if (s_running) {
        return true;
    }

    // wifi_monitor_init()'s uart_tx_task is what actually gets a scan
    // result to the P4 -- if it failed to start (already logged there),
    // starting the scan anyway would report OK while every packet found
    // just sits in s_pkt_queue forever, and the P4 screen waits for data
    // that can never arrive. Fail loudly here instead. See
    // KNOWN_ISSUES.md's Round 19 entry (residual liveness risk).
    if (s_tx_task_handle == NULL || s_pkt_queue == NULL || s_tx_mutex == NULL || s_hop_timer == NULL) {
        ESP_LOGE(TAG, "cannot start: Wi-Fi Monitor initialization was incomplete");
        return false;
    }

    // Promiscuous mode and an actively-connected STA fight over the radio's
    // channel (STA needs to stay on its AP's channel; hopping breaks that),
    // so disconnect first. Deliberately no reconnect attempt afterward --
    // see wifi_monitor.h.
    esp_wifi_disconnect();

    esp_err_t err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_promiscuous(true) failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_wifi_set_promiscuous_rx_cb(promiscuous_rx_cb);

    s_current_channel = 1;
    esp_wifi_set_channel(s_current_channel, WIFI_SECOND_CHAN_NONE);
    xTimerStart(s_hop_timer, 0);

    s_running = true;
    ESP_LOGI(TAG, "Wi-Fi monitor started");
    return true;
}

bool wifi_monitor_stop(void)
{
    if (!s_running) {
        return true;
    }

    s_running = false;
    xTimerStop(s_hop_timer, portMAX_DELAY);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    // Serialize with the TX task before resetting its queue. Therefore once
    // this returns, the command dispatcher may safely send its stop ACK: no
    // old PKT line can be emitted after it.
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    xQueueReset(s_pkt_queue);
    xSemaphoreGive(s_tx_mutex);

    ESP_LOGI(TAG, "Wi-Fi monitor stopped");
    return true;
}
