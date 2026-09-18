#include "wifi_setup_ap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "wifi_commands.h"

#define AP_SSID "MakeshiftFlipper-Setup"
// Open APs let anyone in radio range POST to /connect and hijack which
// network the device joins (and, before the html_escape fix above, run
// script in the setup page via a malicious SSID). A fixed, shipped-in-the-
// firmware PIN would be public knowledge for every unit and wouldn't stop
// a targeted attacker, only casual/opportunistic joins -- so the password
// is generated fresh per setup session by the P4 (which displays it on its
// own OLED) and passed in here via `pin`, rather than being a compile-time
// constant.
// Capped at 1: ESP-IDF's softAP has no real 802.11 client-isolation flag,
// so a second device joining the same AP while setup is in progress could
// sniff the /connect POST -- which carries the real home Wi-Fi password --
// off the air. Limiting to one client at a time closes that window; it's
// not full transport security (still plaintext HTTP), but nobody else can
// be on the AP to listen.
#define AP_MAX_CONN 1

static const char *TAG = "wifi_setup_ap";

#define SETUP_DONE_BIT BIT0
static EventGroupHandle_t s_setup_event_group;
static volatile bool s_setup_succeeded;

// Minimal, dependency-free HTML: a network dropdown populated from a scan
// taken right before the AP starts, plus a password field. No JS framework,
// no external assets -- has to work on a phone browser with no internet.
static char s_networks_html[2048];

// SSIDs come from other people's broadcast beacons, i.e. untrusted input
// that ends up inside an HTML attribute and text node. A hostile SSID like
// `"><script>...` would otherwise inject markup into the setup page on
// whichever phone opens it. Escapes the 5 characters that matter in both
// attribute and text context.
static void html_escape(const char *in, char *out, size_t out_len)
{
    size_t pos = 0;
    for (; *in != '\0' && pos < out_len - 1; in++) {
        const char *rep = NULL;
        switch (*in) {
            case '&':  rep = "&amp;"; break;
            case '<':  rep = "&lt;"; break;
            case '>':  rep = "&gt;"; break;
            case '"':  rep = "&quot;"; break;
            case '\'': rep = "&#39;"; break;
            default: break;
        }
        if (rep != NULL) {
            size_t rep_len = strlen(rep);
            if (pos + rep_len >= out_len) {
                break;
            }
            memcpy(&out[pos], rep, rep_len);
            pos += rep_len;
        } else {
            out[pos++] = *in;
        }
    }
    out[pos] = '\0';
}

static void build_networks_html(void)
{
    wifi_scan_config_t scan_cfg = {0};
    // AP+STA is active by the time this runs, so the STA side can still scan.
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    s_networks_html[0] = '\0';

    if (err != ESP_OK) {
        return;
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count == 0) {
        return;
    }
    if (count > 20) {
        count = 20; // keep the page small
    }

    wifi_ap_record_t *records = malloc(count * sizeof(wifi_ap_record_t));
    if (records == NULL) {
        return;
    }
    esp_wifi_scan_get_ap_records(&count, records);

    size_t pos = 0;
    char escaped[6 * sizeof(records[0].ssid)]; // worst case: every char becomes &quot; (6 bytes)
    for (int i = 0; i < count && pos < sizeof(s_networks_html) - 128; i++) {
        // Same defensive null-terminate as wifi_commands.c's scan handler --
        // uint8_t ssid[33] isn't documented as always null-terminated by
        // ESP-IDF, so don't let html_escape() (or the %s below) run past it.
        records[i].ssid[sizeof(records[i].ssid) - 1] = '\0';
        html_escape((const char *)records[i].ssid, escaped, sizeof(escaped));
        int n = snprintf(&s_networks_html[pos], sizeof(s_networks_html) - pos,
                          "<option value=\"%s\">%s (%d dBm)</option>",
                          escaped, escaped, records[i].rssi);
        // snprintf returns the length it WOULD have written, not the
        // (possibly truncated) length actually written -- adding a
        // negative or over-long value to `pos` unconditionally would let
        // it end up larger than sizeof(s_networks_html), which happens to
        // be harmless today (pos is never used again after this loop) but
        // is the kind of thing that turns into a real overflow the moment
        // someone adds a later write keyed off `pos`. Stop cleanly instead.
        if (n < 0 || (size_t)n >= sizeof(s_networks_html) - pos) {
            break;
        }
        pos += (size_t)n;
    }
    free(records);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char page[2560];
    snprintf(page, sizeof(page),
        "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Wi-Fi Setup</title></head><body>"
        "<h2>Makeshift Flipper - Wi-Fi Setup</h2>"
        "<form method=POST action=/connect>"
        "<label>Network:</label><br>"
        "<select name=ssid required>%s</select><br><br>"
        "<label>Password:</label><br>"
        "<input type=password name=password><br><br>"
        "<button type=submit>Connect</button>"
        "</form></body></html>",
        s_networks_html);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static bool is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Decodes application/x-www-form-urlencoded in place (handles %XX and '+').
// A malformed '%' escape (missing or non-hex digits, e.g. a literal '%' in
// a password) is passed through unchanged rather than guessed at -- silently
// misinterpreting part of a Wi-Fi password would fail the connect attempt
// in a confusing way, so this prefers a predictable no-op over a guess.
static void url_decode(char *s)
{
    char *out = s;
    while (*s) {
        if (*s == '+') {
            *out++ = ' ';
            s++;
        } else if (*s == '%' && is_hex_digit(s[1]) && is_hex_digit(s[2])) {
            char hex[3] = { s[1], s[2], '\0' };
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

static bool extract_form_field(const char *body, const char *key, char *out, size_t out_len)
{
    size_t key_len = strlen(key);
    const char *p = body;
    while (p != NULL) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            p += key_len + 1;
            const char *amp = strchr(p, '&');
            size_t val_len = (amp != NULL) ? (size_t)(amp - p) : strlen(p);
            if (val_len >= out_len) {
                val_len = out_len - 1;
            }
            memcpy(out, p, val_len);
            out[val_len] = '\0';
            url_decode(out);
            return true;
        }
        p = strchr(p, '&');
        if (p != NULL) {
            p++;
        }
    }
    return false;
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char body[256];
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int received = httpd_req_recv(req, body, len);
    if (received <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[64] = {0};
    char password[64] = {0};
    extract_form_field(body, "ssid", ssid, sizeof(ssid));
    extract_form_field(body, "password", password, sizeof(password));

    bool ok = wifi_commands_connect_sta(ssid, password);
    s_setup_succeeded = ok;

    httpd_resp_set_type(req, "text/html");
    if (ok) {
        httpd_resp_send(req, "<html><body><h3>Connected. You can close this page.</h3></body></html>",
                         HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send(req, "<html><body><h3>Failed to connect. Go back and try again.</h3></body></html>",
                         HTTPD_RESP_USE_STRLEN);
    }

    if (ok) {
        xEventGroupSetBits(s_setup_event_group, SETUP_DONE_BIT);
    }
    return ESP_OK;
}

static httpd_handle_t start_http_server(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) != ESP_OK) {
        return NULL;
    }

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_register_uri_handler(server, &root_uri);

    httpd_uri_t connect_uri = {
        .uri = "/connect",
        .method = HTTP_POST,
        .handler = connect_post_handler,
    };
    httpd_register_uri_handler(server, &connect_uri);

    return server;
}

bool wifi_setup_ap_run(const char *pin, int timeout_ms)
{
    if (pin == NULL || strlen(pin) < WIFI_SETUP_AP_PIN_LEN) {
        ESP_LOGE(TAG, "pin too short for WPA2-PSK (need >= %d chars)", WIFI_SETUP_AP_PIN_LEN);
        return false;
    }

    s_setup_event_group = xEventGroupCreate();
    s_setup_succeeded = false;

    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)ap_cfg.ap.password, pin, sizeof(ap_cfg.ap.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    build_networks_html();

    httpd_handle_t server = start_http_server();
    if (server == NULL) {
        ESP_LOGE(TAG, "failed to start HTTP server");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        vEventGroupDelete(s_setup_event_group);
        return false;
    }

    ESP_LOGI(TAG, "Setup AP '%s' up, connect and browse to 192.168.4.1", AP_SSID);

    xEventGroupWaitBits(s_setup_event_group, SETUP_DONE_BIT, pdTRUE, pdFALSE,
                         pdMS_TO_TICKS(timeout_ms));

    httpd_stop(server);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    vEventGroupDelete(s_setup_event_group);

    return s_setup_succeeded;
}
