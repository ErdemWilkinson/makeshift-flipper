#include "wifi_monitor.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

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
} pkt_entry_t;

static QueueHandle_t s_pkt_queue;
static TaskHandle_t s_tx_task_handle;
static TimerHandle_t s_hop_timer;
static volatile bool s_running = false;
static int s_current_channel = 1;

// 802.11 management frame subtypes (frame control byte 0, bits 4-7 of the
// low byte, i.e. the whole byte masked with 0xF0 after the version/type
// bits -- beacon and probe-response are the only ones carrying an SSID
// element we care about here).
#define FRAME_SUBTYPE_BEACON         0x80
#define FRAME_SUBTYPE_PROBE_RESPONSE 0x50
#define FRAME_SUBTYPE_MASK           0xF0

// Sanitizes an SSID for the wire: beacon/probe-response payloads are
// attacker-controllable and may contain the wire protocol's own separators
// or control bytes (unlike every other string this firmware puts on the
// UART, which comes from this codebase or a joystick keyboard with a fixed
// character set). ',' would break PKT's field split, '\n'/'\r' would inject
// a fake line boundary.
static void sanitize_ssid(char *ssid)
{
    for (char *p = ssid; *p != '\0'; p++) {
        if (*p == ',' || *p == '\n' || *p == '\r') {
            *p = '_';
        }
    }
}

// Runs in the Wi-Fi driver's own task context (not an ISR -- ESP-IDF's
// promiscuous RX callback is a normal task callback), so a non-blocking
// xQueueSend is enough; if the queue is full (P4/TX task falling behind)
// the packet is just dropped rather than blocking the Wi-Fi driver.
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
    memcpy(entry.bssid, &payload[10], 6); // addr2 (transmitter/BSSID for these frame types)
    entry.rssi = pkt->rx_ctrl.rssi;
    entry.channel = pkt->rx_ctrl.channel;

    // SSID information element: tag(1)=0x00, length(1), then `length`
    // bytes, starting right after the 12-byte beacon fixed fields (24-byte
    // header + timestamp(8)+interval(2)+capabilities(2)).
    int ie_offset = 36;
    if (ie_offset + 2 > len || payload[ie_offset] != 0x00) {
        return; // not an SSID IE where expected -- malformed/truncated frame
    }
    int ssid_len = payload[ie_offset + 1];
    if (ssid_len > 32 || ie_offset + 2 + ssid_len > len) {
        return;
    }
    memcpy(entry.ssid, &payload[ie_offset + 2], ssid_len);
    entry.ssid[ssid_len] = '\0';
    sanitize_ssid(entry.ssid);

    xQueueSend(s_pkt_queue, &entry, 0);
}

static void uart_tx_task(void *arg)
{
    (void)arg;
    pkt_entry_t entry;
    for (;;) {
        if (xQueueReceive(s_pkt_queue, &entry, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        char line[96];
        snprintf(line, sizeof(line), "PKT:%02X%02X%02X%02X%02X%02X,%s,%d,%d",
                 entry.bssid[0], entry.bssid[1], entry.bssid[2],
                 entry.bssid[3], entry.bssid[4], entry.bssid[5],
                 entry.ssid, entry.rssi, entry.channel);
        uart_link_write_line(line);
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
    xTaskCreate(uart_tx_task, "wifi_mon_tx", 3072, NULL, tskIDLE_PRIORITY + 1, &s_tx_task_handle);
    s_hop_timer = xTimerCreate("wifi_mon_hop", pdMS_TO_TICKS(CHANNEL_HOP_MS),
                                pdTRUE, NULL, hop_timer_cb);
}

bool wifi_monitor_start(void)
{
    if (s_running) {
        return true;
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

    xTimerStop(s_hop_timer, portMAX_DELAY);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    s_running = false;

    ESP_LOGI(TAG, "Wi-Fi monitor stopped");
    return true;
}
