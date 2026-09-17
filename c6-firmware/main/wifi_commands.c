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

#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "uart_link.h"

static const char *TAG = "wifi_commands";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT       BIT1
#define CONNECT_TIMEOUT_MS  10000

// Where the PC running Ollama (https://ollama.com) lives on the local
// network. Ollama's default REST API listens on 11434 and isn't
// network-exposed unless OLLAMA_HOST=0.0.0.0 is set on the PC -- see
// c6-firmware/README.md for the one-time PC-side setup.
//
// Set via "idf.py menuconfig" -> "Makeshift Flipper C6 -- Ask AI (Ollama
// bridge)" (Kconfig.projbuild in this directory) rather than editing this
// file directly -- previously a hardcoded #define here, moved to Kconfig
// so a wrong/stale address doesn't require a source change to fix (see
// KNOWN_ISSUES.md: a stale OLLAMA_HOST after a DHCP reassignment sends
// Ask AI questions to whatever device now holds that address).
#define OLLAMA_HOST CONFIG_MAKESHIFT_OLLAMA_HOST
#define OLLAMA_PORT CONFIG_MAKESHIFT_OLLAMA_PORT
#define OLLAMA_MODEL CONFIG_MAKESHIFT_OLLAMA_MODEL
#define OLLAMA_TIMEOUT_MS 60000
#define OLLAMA_SYSTEM_PROMPT "Sen bir cihaz asistanisin. Kisa ve net turkce cevaplar ver."

// Answers are split into chunks this size (comfortably under
// UART_LINK_MAX_LINE_LEN) before being sent as "ANSWER:<chunk>" lines.
#define ASK_CHUNK_LEN 200

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_netif;

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
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi STA mode ready");
}

void wifi_commands_scan(void)
{
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

    esp_wifi_scan_get_ap_records(&count, records);

    char line[128];
    for (int i = 0; i < count; i++) {
        // wifi_ap_record_t.ssid is uint8_t[33] (32 chars + implicit null
        // slot). ESP-IDF's scan code fills it null-terminated in practice,
        // but that isn't a documented guarantee -- force it explicitly so
        // a non-terminated SSID can't run %s past the array.
        records[i].ssid[sizeof(records[i].ssid) - 1] = '\0';
        snprintf(line, sizeof(line), "NET:%s,%d", (const char *)records[i].ssid, records[i].rssi);
        uart_link_write_line(line);
    }

    free(records);
    uart_link_write_line("SCANDONE");
}

bool wifi_commands_connect_sta(const char *ssid, const char *password)
{
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

// Response body accumulator for the Ollama HTTP client's event callback.
// Was 4096 bytes, which a few-hundred-word answer could exceed (the JSON
// wrapper itself adds overhead on top of the answer text) -- a reply that
// overflowed it got silently truncated mid-JSON, so cJSON_Parse() failed
// and the whole request came back as a bare ASKFAIL with no indication of
// why. Bumped to 16KB (cheap on the C6's RAM budget, comfortably covers a
// long multi-paragraph answer); a reply that still overflows this is
// truncated the same way -- a streaming/incremental JSON parse would be
// needed to handle arbitrarily long answers without any upper bound.
#define ASK_RESPONSE_BUF_LEN 16384

typedef struct {
    char *buf;
    int len;
} ask_response_ctx_t;

static esp_err_t ask_http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        ask_response_ctx_t *ctx = (ask_response_ctx_t *)evt->user_data;
        int copy_len = evt->data_len;
        int space = ASK_RESPONSE_BUF_LEN - 1 - ctx->len;
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

void wifi_commands_ask(const char *question)
{
    char *response_buf = malloc(ASK_RESPONSE_BUF_LEN);
    if (response_buf == NULL) {
        uart_link_write_line("ASKFAIL");
        return;
    }
    response_buf[0] = '\0';
    ask_response_ctx_t ctx = { .buf = response_buf, .len = 0 };

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", OLLAMA_MODEL);
    cJSON_AddStringToObject(req, "prompt", question);
    cJSON_AddStringToObject(req, "system", OLLAMA_SYSTEM_PROMPT);
    cJSON_AddBoolToObject(req, "stream", false);
    char *req_body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    if (req_body == NULL) {
        free(response_buf);
        uart_link_write_line("ASKFAIL");
        return;
    }

    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d/api/generate", OLLAMA_HOST, OLLAMA_PORT);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = OLLAMA_TIMEOUT_MS,
        .event_handler = ask_http_event_handler,
        .user_data = &ctx,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, req_body, strlen(req_body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(req_body);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Ollama request failed: %s (HTTP %d)", esp_err_to_name(err), status);
        free(response_buf);
        uart_link_write_line("ASKFAIL");
        return;
    }

    cJSON *resp = cJSON_Parse(response_buf);
    free(response_buf);
    if (resp == NULL) {
        ESP_LOGW(TAG, "Ollama response wasn't valid JSON (truncated/oversized?)");
        uart_link_write_line("ASKFAIL");
        return;
    }

    cJSON *answer = cJSON_GetObjectItemCaseSensitive(resp, "response");
    if (!cJSON_IsString(answer) || answer->valuestring == NULL) {
        cJSON_Delete(resp);
        uart_link_write_line("ASKFAIL");
        return;
    }

    // Send the answer in fixed-size chunks -- the UART link is line-based
    // and a multi-hundred-token reply would otherwise exceed
    // UART_LINK_MAX_LINE_LEN in one line. A newline inside the answer text
    // itself is replaced with a space first, since the wire protocol uses
    // '\n' strictly as a line terminator.
    const char *text = answer->valuestring;
    size_t text_len = strlen(text);
    char *sanitized = malloc(text_len + 1);
    if (sanitized == NULL) {
        cJSON_Delete(resp);
        uart_link_write_line("ASKFAIL");
        return;
    }
    for (size_t i = 0; i < text_len; i++) {
        char c = text[i];
        sanitized[i] = (c == '\n' || c == '\r') ? ' ' : c;
    }
    sanitized[text_len] = '\0';

    char chunk[ASK_CHUNK_LEN + 8];
    for (size_t offset = 0; offset < text_len; offset += ASK_CHUNK_LEN) {
        size_t n = text_len - offset;
        if (n > ASK_CHUNK_LEN) {
            n = ASK_CHUNK_LEN;
        }
        snprintf(chunk, sizeof(chunk), "ANSWER:%.*s", (int)n, sanitized + offset);
        uart_link_write_line(chunk);
    }
    if (text_len == 0) {
        uart_link_write_line("ANSWER:");
    }
    uart_link_write_line("ANSWERDONE");

    free(sanitized);
    cJSON_Delete(resp);
}
