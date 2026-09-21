#include "wifi_commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "text_sanitize.h"
#include "uart_link.h"

static const char *TAG = "wifi_commands";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT       BIT1
#define CONNECT_TIMEOUT_MS  10000

// Where the log server helper (c6-firmware/tools/debug_server.py) runs on
// the local network -- a small optional HTTP endpoint that just appends
// the device's uploaded error history to a file on a PC, no AI/Ollama
// involved. Entirely optional: the on-device "Errors" history works fully
// offline without this, this is only used when the user explicitly
// chooses "Send" from that screen.
//
// Set via "idf.py menuconfig" -> "Makeshift Flipper C6 -- Error Log"
// (Kconfig.projbuild in this directory) rather than editing this file
// directly, since a wrong/stale address should be a config change, not a
// source change (see KNOWN_ISSUES.md's note on the same risk).
#define LOG_SERVER_HOST CONFIG_MAKESHIFT_LOG_SERVER_HOST
#define LOG_SERVER_PORT CONFIG_MAKESHIFT_LOG_SERVER_PORT
#define LOG_SEND_TIMEOUT_MS 10000 // local POST, no AI call to wait on

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_netif;
static bool s_wifi_ready;

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_commands_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Wi-Fi event group allocation failed; Wi-Fi commands disabled");
        return;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_ready = true;

    ESP_LOGI(TAG, "Wi-Fi STA mode ready");
}

void wifi_commands_scan(void)
{
    if (!s_wifi_ready) {
        ESP_LOGE(TAG, "scan requested before Wi-Fi initialization completed");
        uart_link_write_line("SCANDONE");
        return;
    }
    wifi_scan_config_t scan_cfg = {0};
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true /* block until done */);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan_start failed: %s", esp_err_to_name(err));
        uart_link_write_line("SCANDONE");
        return;
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count == 0) {
        uart_link_write_line("SCANDONE");
        return;
    }

    wifi_ap_record_t *records = malloc(count * sizeof(wifi_ap_record_t));
    if (records == NULL) {
        uart_link_write_line("SCANDONE");
        return;
    }

    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan_get_ap_records failed: %s", esp_err_to_name(err));
        free(records);
        uart_link_write_line("SCANDONE");
        return;
    }

    char line[128];
    for (int i = 0; i < count; i++) {
        // wifi_ap_record_t.ssid is uint8_t[33] (32 chars + implicit null
        // slot). ESP-IDF's scan code fills it null-terminated in practice,
        // but that isn't a documented guarantee -- force it explicitly so
        // a non-terminated SSID can't run %s past the array.
        records[i].ssid[sizeof(records[i].ssid) - 1] = '\0';
        // An SSID is arbitrary 802.11 octets, not text -- a nearby AP can
        // legally broadcast one containing ',' or CR/LF, which would
        // otherwise split or terminate this NET: line early (and, if the
        // user picks that network, corrupt the CONNECT:<ssid>,<password>
        // line sent back later). wifi_monitor.c/bt_scan.c already apply
        // this same sanitize_wire_text() to untrusted SSID/name text on
        // this same UART link; SCAN was missed. This is lossy (the
        // original bytes aren't recoverable), which is fine for display
        // and for re-sending the same sanitized copy back in CONNECT, but
        // means a network whose real SSID needs a comma/CR/LF can't be
        // connected to by name through this protocol -- see
        // KNOWN_ISSUES.md's Round 19 entry.
        sanitize_wire_text((char *)records[i].ssid);
        snprintf(line, sizeof(line), "NET:%s,%d", (const char *)records[i].ssid, records[i].rssi);
        uart_link_write_line(line);
    }

    free(records);
    uart_link_write_line("SCANDONE");
}

bool wifi_commands_connect_sta(const char *ssid, const char *password)
{
    if (!s_wifi_ready) {
        ESP_LOGE(TAG, "connect requested before Wi-Fi initialization completed");
        return false;
    }
    wifi_config_t wifi_cfg = {0};
    size_t ssid_len = strlen(ssid);
    if (ssid_len >= sizeof(wifi_cfg.sta.ssid)) {
        ssid_len = sizeof(wifi_cfg.sta.ssid) - 1;
    }
    memcpy(wifi_cfg.sta.ssid, ssid, ssid_len);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "connect failed to start: %s", esp_err_to_name(err));
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void wifi_commands_connect(const char *args)
{
    // args format: "<ssid>,<password>". SSIDs can't contain commas, so the
    // first comma is always the separator.
    const char *comma = strchr(args, ',');
    if (comma == NULL) {
        uart_link_write_line("FAIL");
        return;
    }

    char ssid[33];
    size_t ssid_len = comma - args;
    if (ssid_len >= sizeof(ssid)) {
        ssid_len = sizeof(ssid) - 1;
    }
    memcpy(ssid, args, ssid_len);
    ssid[ssid_len] = '\0';

    bool ok = wifi_commands_connect_sta(ssid, comma + 1);
    uart_link_write_line(ok ? "OK" : "FAIL");
}

void wifi_commands_send(const char *args)
{
    // args format: "<ip>:<port>:<data>". `data` may itself contain colons,
    // so only the first two are treated as separators.
    char ip[64];
    const char *first_colon = strchr(args, ':');
    if (first_colon == NULL) {
        uart_link_write_line("FAIL");
        return;
    }
    const char *second_colon = strchr(first_colon + 1, ':');
    if (second_colon == NULL) {
        uart_link_write_line("FAIL");
        return;
    }

    size_t ip_len = first_colon - args;
    if (ip_len >= sizeof(ip)) {
        uart_link_write_line("FAIL");
        return;
    }
    memcpy(ip, args, ip_len);
    ip[ip_len] = '\0';

    int port = atoi(first_colon + 1);
    const char *data = second_colon + 1;

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        uart_link_write_line("FAIL");
        return;
    }

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &dest.sin_addr) != 1) {
        close(sock);
        uart_link_write_line("FAIL");
        return;
    }

    struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        close(sock);
        uart_link_write_line("FAIL");
        return;
    }

    int to_send = strlen(data);
    int sent = send(sock, data, to_send, 0);
    close(sock);

    if (sent == to_send) {
        uart_link_write_line("SENT");
    } else {
        uart_link_write_line("FAIL");
    }
}

// Generic HTTP response body accumulator, shared by whatever local HTTP
// call needs one (currently just wifi_commands_log_flush() below). 4KB is
// comfortably over what the log server's small JSON ack ({"status":"ok",
// "count":N}) will ever be.
#define HTTP_RESPONSE_BUF_LEN 4096

typedef struct {
    char *buf;
    int len;
} http_response_ctx_t;

static esp_err_t http_response_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        http_response_ctx_t *ctx = (http_response_ctx_t *)evt->user_data;
        int copy_len = evt->data_len;
        int space = HTTP_RESPONSE_BUF_LEN - 1 - ctx->len;
        if (copy_len > space) {
            copy_len = space;
        }
        if (copy_len > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, copy_len);
            ctx->len += copy_len;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

// Accumulates "LOGSEND:<json>" entries between wifi_commands_log_line()
// calls until wifi_commands_log_flush() sends them all in one POST.
// Capacity matches roughly DIAG_HISTORY_CAPACITY (main/diag/diag.h) worth
// of small JSON objects -- a batch larger than this is truncated (the
// oldest-still-fitting entries are kept, since main.c sends newest-first
// and appends in that order... actually appends in receive order, so this
// buffer just stops accepting once full; see wifi_commands_log_line()).
#define LOG_BATCH_BUF_LEN 4096

static char s_log_batch[LOG_BATCH_BUF_LEN];
static int s_log_batch_len;
static int s_log_entry_count;

void wifi_commands_log_line(const char *json_line)
{
    if (s_log_entry_count == 0) {
        s_log_batch[0] = '\0';
        s_log_batch_len = 0;
    }

    int prefix_len = (s_log_entry_count > 0) ? 1 : 0; // leading ',' between array elements
    int line_len = strlen(json_line);
    int space = LOG_BATCH_BUF_LEN - 1 - s_log_batch_len;
    if (prefix_len + line_len > space) {
        ESP_LOGW(TAG, "Log batch buffer full, dropping entry");
        return;
    }

    if (prefix_len > 0) {
        s_log_batch[s_log_batch_len++] = ',';
    }
    memcpy(s_log_batch + s_log_batch_len, json_line, line_len);
    s_log_batch_len += line_len;
    s_log_batch[s_log_batch_len] = '\0';
    s_log_entry_count++;
}

void wifi_commands_log_flush(void)
{
    if (s_log_entry_count == 0) {
        uart_link_write_line("FAIL"); // nothing to send -- LOGSENDDONE with no LOGSEND: lines first
        return;
    }

    // Wraps the accumulated "{...},{...},..." entries into a JSON object:
    // {"entries":[{...},{...}]}
    char *req_body = malloc(s_log_batch_len + 32);
    bool ok = false;
    if (req_body != NULL) {
        int n = snprintf(req_body, s_log_batch_len + 32, "{\"entries\":[%s]}", s_log_batch);
        ok = (n > 0 && n < s_log_batch_len + 32);
    }
    s_log_entry_count = 0; // reset for the next batch regardless of outcome below

    if (!ok) {
        free(req_body);
        uart_link_write_line("FAIL");
        return;
    }

    char *response_buf = malloc(HTTP_RESPONSE_BUF_LEN);
    if (response_buf == NULL) {
        free(req_body);
        uart_link_write_line("FAIL");
        return;
    }
    response_buf[0] = '\0';
    http_response_ctx_t ctx = { .buf = response_buf, .len = 0 };

    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d/logs", LOG_SERVER_HOST, LOG_SERVER_PORT);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = LOG_SEND_TIMEOUT_MS,
        .event_handler = http_response_event_handler,
        .user_data = &ctx,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        // esp_http_client_init() can return NULL under memory pressure --
        // every call below this dereferences the handle, so unlike the
        // explicit malloc checks above this one, a missed check here
        // wouldn't just fail the upload, it would crash the C6. The log
        // upload is entirely optional (the on-device Errors history works
        // without it), so this should degrade to FAIL, not a reboot -- see
        // KNOWN_ISSUES.md's Round 19 entry.
        ESP_LOGW(TAG, "esp_http_client_init failed (out of memory?)");
        free(req_body);
        free(response_buf);
        uart_link_write_line("FAIL");
        return;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, req_body, strlen(req_body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(req_body);
    free(response_buf);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Log upload failed: %s (HTTP %d)", esp_err_to_name(err), status);
        uart_link_write_line("FAIL");
        return;
    }

    uart_link_write_line("SENT");
}
