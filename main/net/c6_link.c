#include "c6_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/uart.h"
#include "esp_log.h"

// Matches the wiring report: P4 GPIO18(TX)->C6 RX, P4 GPIO19(RX)->C6 TX.
#define UART_PORT UART_NUM_2
#define UART_TX_GPIO 18
#define UART_RX_GPIO 19
#define UART_BAUD 115200

#define LINE_BUF_LEN 256
#define RESPONSE_TIMEOUT_MS 8000  // SCAN/CONNECT can take a few seconds on the C6 side
#define SETUP_TIMEOUT_MS (6 * 60 * 1000) // SETUP waits for a human on the setup page; give it more room than the C6's own 5-minute internal timeout
#define ASK_TIMEOUT_MS (70 * 1000) // give the PC-side LLM (C6's own OLLAMA_TIMEOUT_MS is 60s) a bit of headroom
#define DEBUG_TIMEOUT_MS (75 * 1000) // a bit more than the C6's own DEBUG_TIMEOUT_MS (65s)

static const char *TAG = "c6_link";
static char s_line_buf[LINE_BUF_LEN];

// Every c6_link_* call shares s_line_buf and the one UART port, so only
// one can be in flight at a time. Originally this was safe by
// construction (every call came from the single main-loop task, all
// blocking/synchronous), but the automatic "Debug AI" background task
// (main.c) now calls c6_link_debug() from a second task, which could
// otherwise interleave with e.g. a menu-triggered c6_link_ask() and
// corrupt s_line_buf or the wire protocol. Held for the duration of an
// entire command (write + wait for reply), not just the buffer access.
static SemaphoreHandle_t s_link_mutex;

void c6_link_init(void)
{
    uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_GPIO, UART_RX_GPIO,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    s_link_mutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "C6 link UART initialized (TX=GPIO%d, RX=GPIO%d)", UART_TX_GPIO, UART_RX_GPIO);
}

static bool send_line(const char *line)
{
    size_t len = strlen(line);
    int written = uart_write_bytes(UART_PORT, line, len);
    if (written != (int)len) {
        ESP_LOGW(TAG, "UART write failed/truncated (%d of %d bytes)", written, (int)len);
        return false;
    }
    if (uart_write_bytes(UART_PORT, "\n", 1) != 1) {
        return false;
    }
    return true;
}

// Reads one newline-terminated line into s_line_buf (newline stripped).
// Returns true on success, false on timeout. `deadline_ms` is remaining
// budget, decremented as this polls in small slices.
static bool read_line(int *deadline_ms)
{
    int len = 0;
    while (*deadline_ms > 0) {
        uint8_t byte;
        int got = uart_read_bytes(UART_PORT, &byte, 1, pdMS_TO_TICKS(20));
        if (got != 1) {
            *deadline_ms -= 20;
            continue;
        }
        if (byte == '\n') {
            s_line_buf[len] = '\0';
            return true;
        }
        if (byte != '\r' && len < LINE_BUF_LEN - 1) {
            s_line_buf[len++] = (char)byte;
        }
    }
    return false;
}

static int c6_link_scan_impl(c6_network_t *out_networks, int max_networks)
{
    if (!send_line("SCAN")) {
        return -1;
    }

    int deadline = RESPONSE_TIMEOUT_MS;
    int count = 0;

    while (read_line(&deadline)) {
        if (strcmp(s_line_buf, "SCANDONE") == 0) {
            return count;
        }
        if (strncmp(s_line_buf, "NET:", 4) != 0) {
            continue; // ignore anything unexpected rather than aborting the scan
        }
        if (count >= max_networks) {
            continue; // keep draining lines until SCANDONE, just stop storing
        }

        // Format: NET:<ssid>,<rssi>
        char *comma = strrchr(s_line_buf + 4, ',');
        if (comma == NULL) {
            continue;
        }
        *comma = '\0';
        const char *ssid = s_line_buf + 4;
        int rssi = atoi(comma + 1);

        strncpy(out_networks[count].ssid, ssid, C6_SSID_MAX_LEN);
        out_networks[count].ssid[C6_SSID_MAX_LEN] = '\0';
        out_networks[count].rssi = (int8_t)rssi;
        count++;
    }

    ESP_LOGW(TAG, "SCAN timed out waiting for SCANDONE");
    return -1;
}

static bool c6_link_connect_impl(const char *ssid, const char *password)
{
    // The wire format is "CONNECT:<ssid>,<password>" -- a ',' inside ssid
    // would be indistinguishable from the ssid/password separator and
    // silently split the fields wrong. Reject rather than send a corrupted
    // command (Wi-Fi SSIDs essentially never contain a comma in practice,
    // so this isn't a real-world limitation).
    if (strchr(ssid, ',') != NULL) {
        ESP_LOGE(TAG, "SSID contains ',' -- cannot encode in CONNECT command");
        return false;
    }

    // IEEE 802.11 caps an SSID at 32 bytes. Neither the joystick scroll
    // keyboard (text_entry.c) nor the C6-side web form enforce this on
    // input, they just silently truncate to their own buffer size --
    // catching it here at least fails loudly instead of connecting with a
    // truncated/wrong SSID with no explanation to the user.
    if (strlen(ssid) > C6_SSID_MAX_LEN) {
        ESP_LOGE(TAG, "SSID longer than %d bytes -- rejecting", C6_SSID_MAX_LEN);
        return false;
    }

    char cmd[LINE_BUF_LEN];
    snprintf(cmd, sizeof(cmd), "CONNECT:%s,%s", ssid, password);
    if (!send_line(cmd)) {
        return false;
    }

    int deadline = RESPONSE_TIMEOUT_MS;
    if (!read_line(&deadline)) {
        ESP_LOGW(TAG, "CONNECT timed out");
        return false;
    }
    return strcmp(s_line_buf, "OK") == 0;
}

static bool c6_link_send_impl(const char *ip, uint16_t port, const char *data)
{
    // Wire format "SEND:<ip>:<port>:<data>" -- ip/port never contain ':' in
    // valid IPv4/port values, and the C6 side (wifi_commands.c) only splits
    // on the first two ':' characters, treating everything after as data.
    // So a ':' inside `data` is fine and doesn't need rejecting here.
    char cmd[LINE_BUF_LEN];
    snprintf(cmd, sizeof(cmd), "SEND:%s:%u:%s", ip, port, data);
    if (!send_line(cmd)) {
        return false;
    }

    int deadline = RESPONSE_TIMEOUT_MS;
    if (!read_line(&deadline)) {
        ESP_LOGW(TAG, "SEND timed out");
        return false;
    }
    return strcmp(s_line_buf, "SENT") == 0;
}

static bool c6_link_ask_impl(const char *question, char *out_answer)
{
    out_answer[0] = '\0';

    // Wire format "ASK:<question>". A newline can't appear in `question`
    // (it's typed via the on-device keyboard, which can't produce one), so
    // no escaping is needed here -- unlike CONNECT's ',' or SEND's ':',
    // there's no delimiter inside this command to collide with.
    char cmd[LINE_BUF_LEN];
    snprintf(cmd, sizeof(cmd), "ASK:%s", question);
    if (!send_line(cmd)) {
        return false;
    }

    int deadline = ASK_TIMEOUT_MS;
    int answer_len = 0;
    for (;;) {
        if (!read_line(&deadline)) {
            ESP_LOGW(TAG, "ASK timed out");
            return false;
        }
        if (strcmp(s_line_buf, "ANSWERDONE") == 0) {
            return true;
        }
        if (strcmp(s_line_buf, "ASKFAIL") == 0) {
            return false;
        }
        if (strncmp(s_line_buf, "ANSWER:", 7) != 0) {
            continue; // ignore anything unexpected rather than aborting
        }
        const char *chunk = s_line_buf + 7;
        int chunk_len = strlen(chunk);
        int space = C6_ASK_ANSWER_MAX_LEN - answer_len;
        if (space > 0) {
            if (chunk_len > space) {
                chunk_len = space;
            }
            memcpy(out_answer + answer_len, chunk, chunk_len);
            answer_len += chunk_len;
            out_answer[answer_len] = '\0';
        }
    }
}

static bool c6_link_setup_impl(void)
{
    if (!send_line("SETUP")) {
        return false;
    }

    int deadline = SETUP_TIMEOUT_MS;
    if (!read_line(&deadline)) {
        ESP_LOGW(TAG, "SETUP timed out");
        return false;
    }
    return strcmp(s_line_buf, "OK") == 0;
}

static bool c6_link_debug_impl(const char *module, const char *code, const char *note,
                                c6_debug_result_t *out_result)
{
    out_result->verdict[0] = '\0';
    out_result->explanation[0] = '\0';

    // Wire format "DEBUG:<module>|<code>|<note>" -- '|' can't appear in
    // module/code (short fixed strings from this codebase) or note (typed
    // on the on-device scroll keyboard, which has no '|' key), so no
    // escaping is needed, same reasoning as ASK's lack of '\n' handling.
    char cmd[LINE_BUF_LEN];
    snprintf(cmd, sizeof(cmd), "DEBUG:%s|%s|%s", module, code, note);
    if (!send_line(cmd)) {
        return false;
    }

    int deadline = DEBUG_TIMEOUT_MS;
    if (!read_line(&deadline)) {
        ESP_LOGW(TAG, "DEBUG timed out");
        return false;
    }
    if (strcmp(s_line_buf, "DIAGFAIL") == 0) {
        return false;
    }
    if (strncmp(s_line_buf, "DIAG:", 5) != 0) {
        ESP_LOGW(TAG, "DEBUG: unexpected reply: %s", s_line_buf);
        return false;
    }

    // Reply format "DIAG:<verdict>|<explanation>".
    const char *body = s_line_buf + 5;
    const char *bar = strchr(body, '|');
    if (bar == NULL) {
        ESP_LOGW(TAG, "DEBUG: malformed DIAG reply (no '|')");
        return false;
    }

    size_t verdict_len = bar - body;
    if (verdict_len > C6_DEBUG_VERDICT_MAX_LEN) {
        verdict_len = C6_DEBUG_VERDICT_MAX_LEN;
    }
    memcpy(out_result->verdict, body, verdict_len);
    out_result->verdict[verdict_len] = '\0';

    strncpy(out_result->explanation, bar + 1, C6_DEBUG_EXPLANATION_MAX_LEN);
    out_result->explanation[C6_DEBUG_EXPLANATION_MAX_LEN] = '\0';

    return true;
}

// --- Public API: each wraps its _impl with s_link_mutex, so only one
// command (from whichever task) is ever using the UART/s_line_buf at a
// time. Waits forever for the lock rather than timing out -- every _impl
// already has its own timeout, so a caller can't get stuck longer than
// that plus however long whatever's currently holding the lock takes.

int c6_link_scan(c6_network_t *out_networks, int max_networks)
{
    xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    int result = c6_link_scan_impl(out_networks, max_networks);
    xSemaphoreGive(s_link_mutex);
    return result;
}

bool c6_link_connect(const char *ssid, const char *password)
{
    xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    bool result = c6_link_connect_impl(ssid, password);
    xSemaphoreGive(s_link_mutex);
    return result;
}

bool c6_link_send(const char *ip, uint16_t port, const char *data)
{
    xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    bool result = c6_link_send_impl(ip, port, data);
    xSemaphoreGive(s_link_mutex);
    return result;
}

bool c6_link_ask(const char *question, char *out_answer)
{
    xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    bool result = c6_link_ask_impl(question, out_answer);
    xSemaphoreGive(s_link_mutex);
    return result;
}

bool c6_link_setup(void)
{
    xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    bool result = c6_link_setup_impl();
    xSemaphoreGive(s_link_mutex);
    return result;
}

bool c6_link_debug(const char *module, const char *code, const char *note,
                    c6_debug_result_t *out_result)
{
    xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    bool result = c6_link_debug_impl(module, code, note, out_result);
    xSemaphoreGive(s_link_mutex);
    return result;
}
