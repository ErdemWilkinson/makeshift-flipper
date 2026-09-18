#include "c6_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "json_escape.h"

// Matches the wiring report: P4 GPIO18(TX)->C6 RX, P4 GPIO19(RX)->C6 TX.
#define UART_PORT UART_NUM_2
#define UART_TX_GPIO 18
#define UART_RX_GPIO 19
#define UART_BAUD 115200

#define LINE_BUF_LEN 256
#define RESPONSE_TIMEOUT_MS 8000  // SCAN/CONNECT can take a few seconds on the C6 side
#define SETUP_TIMEOUT_MS (6 * 60 * 1000) // SETUP waits for a human on the setup page; give it more room than the C6's own 5-minute internal timeout

static const char *TAG = "c6_link";
static char s_line_buf[LINE_BUF_LEN];

// Every c6_link_* call shares s_line_buf and the one UART port, so only
// one can be in flight at a time. Originally this was safe by
// construction (every call came from the single main-loop task, all
// blocking/synchronous); Wi-Fi Monitor/BT Scan's background collector
// tasks (further down) now also share it, which is why every command --
// including the ones below -- goes through this mutex, held for the
// duration of an entire command (write + wait for reply), not just the
// buffer access.
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

static bool c6_link_setup_impl(const char *pin)
{
    char cmd[LINE_BUF_LEN];
    int n = snprintf(cmd, sizeof(cmd), "SETUP:%s", pin);
    if (n < 0 || n >= (int)sizeof(cmd)) {
        return false;
    }
    if (!send_line(cmd)) {
        return false;
    }

    int deadline = SETUP_TIMEOUT_MS;
    if (!read_line(&deadline)) {
        ESP_LOGW(TAG, "SETUP timed out");
        return false;
    }
    return strcmp(s_line_buf, "OK") == 0;
}

static bool c6_link_send_error_log_impl(const diag_entry_t *entries, int count)
{
    int64_t now_us = esp_timer_get_time();

    for (int i = 0; i < count; i++) {
        // One JSON object per "LOGSEND:" line (newline-delimited JSON) --
        // the whole history won't fit in one UART line, same reasoning as
        // ASK's old chunking. `ago_s` is relative to "now" on the P4 side,
        // not a wall-clock timestamp (there isn't one -- see diag.h).
        int64_t ago_s = (now_us - entries[i].timestamp_us) / 1000000;

        char module_esc[DIAG_MODULE_MAX_LEN * 2 + 1];
        size_t module_len = 0;
        json_escape_append(module_esc, sizeof(module_esc), &module_len, entries[i].module);

        char code_esc[DIAG_CODE_MAX_LEN * 2 + 1];
        size_t code_len = 0;
        json_escape_append(code_esc, sizeof(code_esc), &code_len, entries[i].code);

        char cmd[LINE_BUF_LEN];
        int n = snprintf(cmd, sizeof(cmd), "LOGSEND:{\"module\":\"%s\",\"code\":\"%s\",\"ago_s\":%lld}",
                          module_esc, code_esc, (long long)ago_s);
        if (n < 0 || n >= (int)sizeof(cmd)) {
            continue; // shouldn't happen given the field sizes above, but don't send a truncated line
        }
        if (!send_line(cmd)) {
            return false;
        }
    }

    if (!send_line("LOGSENDDONE")) {
        return false;
    }

    int deadline = C6_LOGSEND_TIMEOUT_MS;
    if (!read_line(&deadline)) {
        ESP_LOGW(TAG, "LOGSEND timed out");
        return false;
    }
    return strcmp(s_line_buf, "SENT") == 0;
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

bool c6_link_setup(const char *pin)
{
    xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    bool result = c6_link_setup_impl(pin);
    xSemaphoreGive(s_link_mutex);
    return result;
}

bool c6_link_send_error_log(const diag_entry_t *entries, int count)
{
    xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    bool result = c6_link_send_error_log_impl(entries, count);
    xSemaphoreGive(s_link_mutex);
    return result;
}

// --- Wi-Fi Monitor -----------------------------------------------------
// See c6_link.h's comment on c6_link_monitor_start(): this doesn't fit the
// request/reply _impl pattern above because the C6 keeps pushing "PKT:"
// lines indefinitely after the initial "OK". s_monitor_rx_task reads those
// (and only those -- anything else is logged and ignored, same as the
// other _impl functions' "ignore unexpected lines" habit) while holding
// s_link_mutex for the task's entire lifetime, which is what blocks every
// other c6_link_* call while a monitor session is active.

static TaskHandle_t s_monitor_rx_task_handle;
static volatile bool s_monitor_running = false;
static SemaphoreHandle_t s_monitor_data_mutex; // guards s_monitor_aps/_count only
static c6_monitor_ap_t s_monitor_aps[C6_MONITOR_MAX_APS];
static int s_monitor_ap_count;

// Declared here (ahead of its own section further down) so
// c6_link_monitor_start() can check it -- Wi-Fi Monitor and BT Scan share
// the same "one background task holds s_link_mutex for the session" slot
// and can't run at the same time, see both start functions.
static volatile bool s_bt_scan_running = false;

// Format: "PKT:<bssid_hex12>,<ssid>,<rssi>,<channel>". Malformed lines are
// dropped silently -- a stray non-PKT line (there shouldn't be one, since
// nothing else talks to the C6 while this task owns s_link_mutex) is
// likewise just ignored rather than treated as fatal.
static void handle_pkt_line(const char *line)
{
    const char *body = line + 4; // skip "PKT:"

    if (strlen(body) < 12 || body[12] != ',') {
        return;
    }
    uint8_t bssid[6];
    for (int i = 0; i < 6; i++) {
        unsigned int byte;
        if (sscanf(body + i * 2, "%2x", &byte) != 1) {
            return;
        }
        bssid[i] = (uint8_t)byte;
    }

    const char *ssid_start = body + 13;
    const char *comma1 = strchr(ssid_start, ',');
    if (comma1 == NULL) {
        return;
    }
    const char *comma2 = strchr(comma1 + 1, ',');
    if (comma2 == NULL) {
        return;
    }

    char ssid[C6_MONITOR_SSID_MAX_LEN + 1];
    size_t ssid_len = comma1 - ssid_start;
    if (ssid_len > C6_MONITOR_SSID_MAX_LEN) {
        ssid_len = C6_MONITOR_SSID_MAX_LEN;
    }
    memcpy(ssid, ssid_start, ssid_len);
    ssid[ssid_len] = '\0';

    int rssi = atoi(comma1 + 1);
    int channel = atoi(comma2 + 1);

    xSemaphoreTake(s_monitor_data_mutex, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < s_monitor_ap_count; i++) {
        if (memcmp(s_monitor_aps[i].bssid, bssid, 6) == 0) {
            slot = i;
            break;
        }
    }
    if (slot == -1 && s_monitor_ap_count < C6_MONITOR_MAX_APS) {
        slot = s_monitor_ap_count++;
        memcpy(s_monitor_aps[slot].bssid, bssid, 6);
    }
    if (slot != -1) {
        strncpy(s_monitor_aps[slot].ssid, ssid, C6_MONITOR_SSID_MAX_LEN);
        s_monitor_aps[slot].ssid[C6_MONITOR_SSID_MAX_LEN] = '\0';
        s_monitor_aps[slot].rssi = (int8_t)rssi;
        s_monitor_aps[slot].channel = (uint8_t)channel;
    }
    xSemaphoreGive(s_monitor_data_mutex);
}

static void monitor_rx_task(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_link_mutex, portMAX_DELAY);

    while (s_monitor_running) {
        int deadline = 1000; // short slices so the s_monitor_running check is responsive
        if (!read_line(&deadline)) {
            continue; // just a quiet second, not a real timeout -- keep looping
        }
        if (strncmp(s_line_buf, "PKT:", 4) == 0) {
            handle_pkt_line(s_line_buf);
        }
    }

    send_line("MONITORSTOP");
    int deadline = RESPONSE_TIMEOUT_MS;
    read_line(&deadline); // best-effort -- nothing to do differently either way

    xSemaphoreGive(s_link_mutex);
    s_monitor_rx_task_handle = NULL;
    vTaskDelete(NULL);
}

bool c6_link_monitor_start(void)
{
    if (s_monitor_running || s_bt_scan_running) {
        // s_link_mutex is held for the whole session by whichever of
        // monitor_rx_task/bt_scan_rx_task is already running -- starting
        // the other here would just deadlock waiting for it.
        return false;
    }

    xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    bool ok = send_line("MONITOR");
    if (ok) {
        int deadline = RESPONSE_TIMEOUT_MS;
        ok = read_line(&deadline) && strcmp(s_line_buf, "OK") == 0;
    }
    xSemaphoreGive(s_link_mutex);

    if (!ok) {
        return false;
    }

    if (s_monitor_data_mutex == NULL) {
        s_monitor_data_mutex = xSemaphoreCreateMutex();
    }
    s_monitor_ap_count = 0;
    s_monitor_running = true;
    xTaskCreate(monitor_rx_task, "c6_monitor_rx", 4096, NULL,
                tskIDLE_PRIORITY + 1, &s_monitor_rx_task_handle);
    return true;
}

bool c6_link_monitor_stop(void)
{
    if (!s_monitor_running) {
        return true;
    }
    // monitor_rx_task itself sends "MONITORSTOP" and releases s_link_mutex
    // once it notices this flag -- not done here, since this function
    // doesn't (and shouldn't) hold s_link_mutex itself while that task is
    // using it.
    s_monitor_running = false;
    return true;
}

int c6_link_monitor_poll(c6_monitor_ap_t *out_aps, int max_aps)
{
    if (s_monitor_data_mutex == NULL) {
        return 0;
    }
    xSemaphoreTake(s_monitor_data_mutex, portMAX_DELAY);
    int count = s_monitor_ap_count;
    if (count > max_aps) {
        count = max_aps;
    }
    memcpy(out_aps, s_monitor_aps, count * sizeof(c6_monitor_ap_t));
    xSemaphoreGive(s_monitor_data_mutex);
    return count;
}

// --- BT Scan -------------------------------------------------------------
// Mirrors the Wi-Fi Monitor section above exactly -- same "background task
// holds s_link_mutex for the session" shape, same reasons (see
// c6_link.h's comment on c6_link_bt_scan_start()). Kept as a fully
// separate task/state rather than generalizing the two into one, since
// the wire format (BTDEV vs PKT) and payload shape differ enough that a
// shared implementation would mostly be an extra layer of indirection
// over two near-identical copies anyway.

static TaskHandle_t s_bt_scan_rx_task_handle;
static SemaphoreHandle_t s_bt_scan_data_mutex; // guards s_bt_devices/_count only
static c6_bt_device_t s_bt_devices[C6_BT_MAX_DEVICES];
static int s_bt_device_count;

// Format: "BTDEV:<addr_hex12>,<name>,<rssi>". Same "drop malformed lines
// silently" handling as handle_pkt_line().
static void handle_btdev_line(const char *line)
{
    const char *body = line + 6; // skip "BTDEV:"

    if (strlen(body) < 12 || body[12] != ',') {
        return;
    }
    uint8_t addr[6];
    for (int i = 0; i < 6; i++) {
        unsigned int byte;
        if (sscanf(body + i * 2, "%2x", &byte) != 1) {
            return;
        }
        addr[i] = (uint8_t)byte;
    }

    const char *name_start = body + 13;
    const char *comma = strrchr(name_start, ',');
    if (comma == NULL) {
        return;
    }

    char name[C6_BT_NAME_MAX_LEN + 1];
    size_t name_len = comma - name_start;
    if (name_len > C6_BT_NAME_MAX_LEN) {
        name_len = C6_BT_NAME_MAX_LEN;
    }
    memcpy(name, name_start, name_len);
    name[name_len] = '\0';

    int rssi = atoi(comma + 1);

    xSemaphoreTake(s_bt_scan_data_mutex, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < s_bt_device_count; i++) {
        if (memcmp(s_bt_devices[i].addr, addr, 6) == 0) {
            slot = i;
            break;
        }
    }
    if (slot == -1 && s_bt_device_count < C6_BT_MAX_DEVICES) {
        slot = s_bt_device_count++;
        memcpy(s_bt_devices[slot].addr, addr, 6);
    }
    if (slot != -1) {
        strncpy(s_bt_devices[slot].name, name, C6_BT_NAME_MAX_LEN);
        s_bt_devices[slot].name[C6_BT_NAME_MAX_LEN] = '\0';
        s_bt_devices[slot].rssi = (int8_t)rssi;
    }
    xSemaphoreGive(s_bt_scan_data_mutex);
}

static void bt_scan_rx_task(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_link_mutex, portMAX_DELAY);

    while (s_bt_scan_running) {
        int deadline = 1000;
        if (!read_line(&deadline)) {
            continue;
        }
        if (strncmp(s_line_buf, "BTDEV:", 6) == 0) {
            handle_btdev_line(s_line_buf);
        }
    }

    send_line("BTSCANSTOP");
    int deadline = RESPONSE_TIMEOUT_MS;
    read_line(&deadline); // best-effort

    xSemaphoreGive(s_link_mutex);
    s_bt_scan_rx_task_handle = NULL;
    vTaskDelete(NULL);
}

bool c6_link_bt_scan_start(void)
{
    if (s_bt_scan_running || s_monitor_running) {
        return false;
    }

    xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    bool ok = send_line("BTSCAN");
    if (ok) {
        int deadline = RESPONSE_TIMEOUT_MS;
        ok = read_line(&deadline) && strcmp(s_line_buf, "OK") == 0;
    }
    xSemaphoreGive(s_link_mutex);

    if (!ok) {
        return false;
    }

    if (s_bt_scan_data_mutex == NULL) {
        s_bt_scan_data_mutex = xSemaphoreCreateMutex();
    }
    s_bt_device_count = 0;
    s_bt_scan_running = true;
    xTaskCreate(bt_scan_rx_task, "c6_bt_scan_rx", 4096, NULL,
                tskIDLE_PRIORITY + 1, &s_bt_scan_rx_task_handle);
    return true;
}

bool c6_link_bt_scan_stop(void)
{
    if (!s_bt_scan_running) {
        return true;
    }
    s_bt_scan_running = false;
    return true;
}

int c6_link_bt_scan_poll(c6_bt_device_t *out_devices, int max_devices)
{
    if (s_bt_scan_data_mutex == NULL) {
        return 0;
    }
    xSemaphoreTake(s_bt_scan_data_mutex, portMAX_DELAY);
    int count = s_bt_device_count;
    if (count > max_devices) {
        count = max_devices;
    }
    memcpy(out_devices, s_bt_devices, count * sizeof(c6_bt_device_t));
    xSemaphoreGive(s_bt_scan_data_mutex);
    return count;
}
