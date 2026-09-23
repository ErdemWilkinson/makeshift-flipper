#include "c6_link.h"

// ---------------------------------------------------------------------------
// STAGE 2 -- direct on-chip radio (C6-standalone).
//
// In the original two-chip design this file was the P4-side client that
// talked to a separate ESP32-C6 over UART, and every call sent a command
// line and parsed reply lines. There is no second chip here: the ESP32-C6
// runs the UI AND the radio, so this file now calls esp_wifi_* directly and
// fills the SAME c6_link_* API the UI already uses. main.c is unchanged.
//
// What maps to what, vs. the old c6-firmware/ UART handlers:
//   c6_link_init      <- wifi_commands_init      (netif + esp_wifi STA up)
//   c6_link_scan      <- wifi_commands_scan       (blocking scan, returns list)
//   c6_link_connect   <- wifi_commands_connect_sta(blocking join)
//   c6_link_monitor_* <- wifi_monitor.c           (promiscuous + channel hop)
//   c6_link_bt_scan_* <- bt_scan.c                (BLE scan -- see radio_ble.c)
//
// The old UART-push model (a background task writing "PKT:" lines) is gone:
// the promiscuous callback writes straight into an in-RAM, BSSID-deduplicated
// list, and c6_link_monitor_poll() copies a snapshot of that list -- which is
// exactly the polling shape main.c's monitor screen already expects.
//
// BLE (c6_link_bt_scan_*) lives in radio_ble.c to keep the NimBLE headers out
// of this translation unit; the c6_link_bt_scan_* entry points there fill the
// same API.
// ---------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

static const char *TAG = "c6_link";

// Defined in radio_ble.c -- brings up the NimBLE stack for BT Scan. Declared
// here (rather than in a header) so c6_link_init() can start it right after
// Wi-Fi without main.c needing a second init call.
void c6_bt_init(void);
bool c6_bt_scan_is_running(void);

// --- Wi-Fi station bring-up / connect bookkeeping --------------------------
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define CONNECT_TIMEOUT_MS 10000

static EventGroupHandle_t s_wifi_event_group;
static bool s_wifi_ready;
static volatile bool s_monitor_running;

// Replace control bytes that would corrupt on-screen rendering. Commas no
// longer need escaping because the standalone build has no UART protocol.
static void sanitize_ssid(char *s)
{
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7F) {
            *s = '?';
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(err));
        return false;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "default Wi-Fi STA netif allocation failed");
        return false;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return false;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "event group alloc failed; Wi-Fi disabled");
        return false;
    }
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     &wifi_event_handler, NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         &wifi_event_handler, NULL);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi STA setup failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Wi-Fi STA ready (on-chip radio)");
    return true;
}

void c6_link_init(void)
{
    // NVS is already initialized by app_main(). A radio failure must not
    // prevent the local UI/RFID/IR features from booting.
    s_wifi_ready = wifi_init();

    // BLE initialization is independent of Wi-Fi initialization.
    c6_bt_init();
}

int c6_link_scan(c6_network_t *out_networks, int max_networks)
{
    if (!s_wifi_ready || s_monitor_running || c6_bt_scan_is_running() ||
        out_networks == NULL || max_networks <= 0) {
        return -1;
    }
    // A monitor session owns the radio's channel; a normal scan can't run
    // alongside it. Callers gate on this too, but guard here as well.
    wifi_scan_config_t scan_cfg = {0};
    if (esp_wifi_scan_start(&scan_cfg, true /* block */) != ESP_OK) {
        return -1;
    }

    uint16_t count = 0;
    if (esp_wifi_scan_get_ap_num(&count) != ESP_OK) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (count > max_networks) {
        count = (uint16_t)max_networks;
    }

    wifi_ap_record_t *records = malloc(count * sizeof(wifi_ap_record_t));
    if (records == NULL) {
        return -1;
    }
    uint16_t got = count;
    if (esp_wifi_scan_get_ap_records(&got, records) != ESP_OK) {
        free(records);
        return -1;
    }

    int n = 0;
    for (int i = 0; i < got && n < max_networks; i++) {
        records[i].ssid[sizeof(records[i].ssid) - 1] = '\0';
        strncpy(out_networks[n].ssid, (const char *)records[i].ssid,
                C6_SSID_MAX_LEN);
        out_networks[n].ssid[C6_SSID_MAX_LEN] = '\0';
        sanitize_ssid(out_networks[n].ssid);
        out_networks[n].rssi = records[i].rssi;
        n++;
    }
    free(records);
    return n;
}

bool c6_link_connect(const char *ssid, const char *password)
{
    if (!s_wifi_ready || s_monitor_running || c6_bt_scan_is_running() ||
        s_wifi_event_group == NULL || ssid == NULL || password == NULL) {
        return false;
    }
    size_t password_len = strlen(password);
    if (ssid[0] == '\0' || (password_len > 0 && password_len < 8) ||
        password_len > 63) {
        return false;
    }
    wifi_config_t cfg = {0};
    size_t ssid_len = strlen(ssid);
    if (ssid_len >= sizeof(cfg.sta.ssid)) {
        ssid_len = sizeof(cfg.sta.ssid) - 1;
    }
    memcpy(cfg.sta.ssid, ssid, ssid_len);
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        return false;
    }
    if (esp_wifi_connect() != ESP_OK) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool c6_link_send(const char *ip, uint16_t port, const char *data)
{
    // TCP send is not part of any current UI action on the standalone build
    // (it existed for a PC-side helper flow). Left unimplemented for now --
    // returns false so any future caller sees a clean "failed" rather than a
    // silent success. Port the lwip socket code from the old
    // wifi_commands_send() here if a UI feature ever needs it.
    (void)ip;
    (void)port;
    (void)data;
    return false;
}

bool c6_link_setup(const char *pin)
{
    // The web-based Wi-Fi setup AP (old wifi_setup_ap.c) is a larger piece
    // (HTTP server + captive portal) deferred past this radio bring-up. Until
    // it's ported, the UI explicitly reports it unavailable; WiFi Setup
    // Manual (scan + connect above) is the working path.
    (void)pin;
    return false;
}

bool c6_link_send_error_log(const diag_entry_t *entries, int count)
{
    // Depended on the PC-side HTTP log collector over the old link. Not part
    // of standalone bring-up; the on-device Errors history works fully
    // offline without it.
    (void)entries;
    (void)count;
    return false;
}

// --- Wi-Fi Monitor (promiscuous) -------------------------------------------
#define MONITOR_CHANNEL_COUNT 13
#define MONITOR_CHANNEL_HOP_MS 400
#define FRAME_SUBTYPE_BEACON         0x80
#define FRAME_SUBTYPE_PROBE_RESPONSE 0x50
#define FRAME_SUBTYPE_MASK           0xF0

static int s_monitor_channel = 1;
static TimerHandle_t s_hop_timer;
static SemaphoreHandle_t s_monitor_lock; // guards the AP list below

// In-RAM deduplicated AP list. Replaces the old UART PKT-push task: the
// promiscuous callback updates this in place (by BSSID), and
// c6_link_monitor_poll() snapshots it. Capped at C6_MONITOR_MAX_APS; a new
// BSSID past the cap is dropped, a repeat sighting updates rssi/channel.
static c6_monitor_ap_t s_monitor_aps[C6_MONITOR_MAX_APS];
static int s_monitor_ap_count;

static const char *parse_security_mode(const uint8_t *payload, int len)
{
    if (len < 36) {
        return "UNKNOWN";
    }
    uint16_t capability = (uint16_t)payload[34] |
                          ((uint16_t)payload[35] << 8);
    bool privacy = (capability & 0x0010) != 0;
    if (!privacy) {
        return "OPEN";
    }

    bool rsn = false;
    bool wpa = false;
    bool wpa3 = false;
    int i = 36; // past fixed beacon fields
    while (i + 2 <= len) {
        uint8_t id = payload[i];
        uint8_t elen = payload[i + 1];
        if (i + 2 + elen > len) {
            break;
        }
        if (id == 0x30) {
            rsn = true; // RSN element => WPA2/WPA3
            const uint8_t *data = &payload[i + 2];
            if (elen >= 10) {
                uint16_t pairwise_count = (uint16_t)data[6] |
                                          ((uint16_t)data[7] << 8);
                int akm_count_offset = 8 + 4 * pairwise_count;
                if (akm_count_offset + 2 <= elen) {
                    uint16_t akm_count = (uint16_t)data[akm_count_offset] |
                                         ((uint16_t)data[akm_count_offset + 1] << 8);
                    int akm = akm_count_offset + 2;
                    for (int k = 0; k < akm_count && akm + 4 <= elen; k++, akm += 4) {
                        if (data[akm] == 0x00 && data[akm + 1] == 0x0F &&
                            data[akm + 2] == 0xAC &&
                            (data[akm + 3] == 8 || data[akm + 3] == 24)) {
                            wpa3 = true; // SAE / SAE-EXT
                        }
                    }
                }
            }
        } else if (id == 0xDD && elen >= 4 &&
                   payload[i + 2] == 0x00 && payload[i + 3] == 0x50 &&
                   payload[i + 4] == 0xF2 && payload[i + 5] == 0x01) {
            wpa = true; // Microsoft WPA IE
        }
        i += 2 + elen;
    }
    if (wpa3) {
        return "WPA3";
    }
    if (rsn) {
        return "WPA2";
    }
    if (wpa) {
        return "WPA";
    }
    return "WEP";
}

// Insert-or-update one AP in the deduped list. Caller holds s_monitor_lock.
static void monitor_upsert(const c6_monitor_ap_t *ap)
{
    for (int i = 0; i < s_monitor_ap_count; i++) {
        if (memcmp(s_monitor_aps[i].bssid, ap->bssid, 6) == 0) {
            s_monitor_aps[i].rssi = ap->rssi;
            s_monitor_aps[i].channel = ap->channel;
            if (ap->ssid[0] != '\0') {
                memcpy(s_monitor_aps[i].ssid, ap->ssid, sizeof(ap->ssid));
            }
            if (ap->sec[0] != '\0') {
                memcpy(s_monitor_aps[i].sec, ap->sec, sizeof(ap->sec));
            }
            return;
        }
    }
    if (s_monitor_ap_count < C6_MONITOR_MAX_APS) {
        s_monitor_aps[s_monitor_ap_count++] = *ap;
    }
}

static void monitor_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT || !s_monitor_running) {
        return;
    }
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *payload = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 36) {
        return;
    }
    uint8_t subtype = payload[0] & FRAME_SUBTYPE_MASK;
    if (subtype != FRAME_SUBTYPE_BEACON && subtype != FRAME_SUBTYPE_PROBE_RESPONSE) {
        return;
    }

    c6_monitor_ap_t ap = {0};
    memcpy(ap.bssid, &payload[10], 6); // addr2 (BSSID)
    ap.rssi = pkt->rx_ctrl.rssi;
    ap.channel = pkt->rx_ctrl.channel;

    for (int ie = 36; ie + 2 <= len;) {
        uint8_t id = payload[ie];
        uint8_t ie_len = payload[ie + 1];
        if (ie + 2 + ie_len > len) {
            break;
        }
        if (id == 0x00 && ie_len <= C6_MONITOR_SSID_MAX_LEN) {
            memcpy(ap.ssid, &payload[ie + 2], ie_len);
            ap.ssid[ie_len] = '\0';
            sanitize_ssid(ap.ssid);
            break;
        }
        ie += 2 + ie_len;
    }
    const char *sec = parse_security_mode(payload, len);
    strncpy(ap.sec, sec, sizeof(ap.sec) - 1);

    // The callback runs in Wi-Fi task context; take the lock briefly. Using
    // a mutex here is safe because this is not an ISR.
    if (s_monitor_lock != NULL &&
        xSemaphoreTake(s_monitor_lock, 0) == pdTRUE) {
        monitor_upsert(&ap);
        xSemaphoreGive(s_monitor_lock);
    }
}

static void hop_timer_cb(TimerHandle_t t)
{
    (void)t;
    s_monitor_channel = (s_monitor_channel % MONITOR_CHANNEL_COUNT) + 1;
    esp_wifi_set_channel(s_monitor_channel, WIFI_SECOND_CHAN_NONE);
}

bool c6_link_monitor_start(void)
{
    if (!s_wifi_ready || c6_bt_scan_is_running()) {
        return false;
    }
    if (s_monitor_running) {
        return true;
    }
    if (s_monitor_lock == NULL) {
        s_monitor_lock = xSemaphoreCreateMutex();
    }
    if (s_hop_timer == NULL) {
        s_hop_timer = xTimerCreate("c6_mon_hop",
                                   pdMS_TO_TICKS(MONITOR_CHANNEL_HOP_MS),
                                   pdTRUE, NULL, hop_timer_cb);
    }
    if (s_monitor_lock == NULL || s_hop_timer == NULL) {
        return false;
    }

    // Clear last session's list.
    if (xSemaphoreTake(s_monitor_lock, portMAX_DELAY) == pdTRUE) {
        s_monitor_ap_count = 0;
        xSemaphoreGive(s_monitor_lock);
    }

    // Promiscuous mode and a connected STA fight over the channel; drop any
    // active connection first (no auto-reconnect, same as the old design).
    esp_wifi_disconnect();

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT,
    };
    esp_err_t err = esp_wifi_set_promiscuous_filter(&filter);
    if (err != ESP_OK) {
        return false;
    }
    err = esp_wifi_set_promiscuous_rx_cb(monitor_rx_cb);
    if (err != ESP_OK) {
        return false;
    }
    err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) {
        esp_wifi_set_promiscuous_rx_cb(NULL);
        return false;
    }
    s_monitor_channel = 1;
    err = esp_wifi_set_channel(s_monitor_channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(NULL);
        return false;
    }
    s_monitor_running = true;
    if (xTimerStart(s_hop_timer, 0) != pdPASS) {
        s_monitor_running = false;
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(NULL);
        return false;
    }
    ESP_LOGI(TAG, "Wi-Fi monitor started");
    return true;
}

bool c6_link_monitor_stop(void)
{
    if (!s_monitor_running) {
        return true;
    }
    s_monitor_running = false;
    if (s_hop_timer != NULL) {
        xTimerStop(s_hop_timer, portMAX_DELAY);
    }
    esp_err_t promiscuous_err = esp_wifi_set_promiscuous(false);
    esp_err_t callback_err = esp_wifi_set_promiscuous_rx_cb(NULL);
    ESP_LOGI(TAG, "Wi-Fi monitor stopped");
    return promiscuous_err == ESP_OK && callback_err == ESP_OK;
}

bool c6_wifi_monitor_is_running(void)
{
    return s_monitor_running;
}

int c6_link_monitor_poll(c6_monitor_ap_t *out_aps, int max_aps)
{
    if (out_aps == NULL || max_aps <= 0 || s_monitor_lock == NULL) {
        return 0;
    }
    int n = 0;
    if (xSemaphoreTake(s_monitor_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        n = s_monitor_ap_count < max_aps ? s_monitor_ap_count : max_aps;
        memcpy(out_aps, s_monitor_aps, n * sizeof(c6_monitor_ap_t));
        xSemaphoreGive(s_monitor_lock);
    }
    return n;
}
