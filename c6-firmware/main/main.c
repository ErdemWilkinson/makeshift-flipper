#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "bt_scan.h"
#include "uart_link.h"
#include "wifi_commands.h"
#include "wifi_monitor.h"
#include "wifi_setup_ap.h"

#define SETUP_TIMEOUT_MS (5 * 60 * 1000) // 5 minutes to find and use the AP

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    uart_link_init();
    wifi_commands_init();
    wifi_monitor_init();
    bt_scan_init();

    ESP_LOGI(TAG, "Ready, waiting for commands from P4");

    char line[UART_LINK_MAX_LINE_LEN];
    for (;;) {
        uart_link_read_line(line);

        if (strcmp(line, "SCAN") == 0) {
            wifi_commands_scan();
        } else if (strncmp(line, "CONNECT:", 8) == 0) {
            wifi_commands_connect(line + 8);
        } else if (strncmp(line, "SEND:", 5) == 0) {
            wifi_commands_send(line + 5);
        } else if (strncmp(line, "LOGSEND:", 8) == 0) {
            wifi_commands_log_line(line + 8);
        } else if (strcmp(line, "LOGSENDDONE") == 0) {
            wifi_commands_log_flush();
        } else if (strcmp(line, "MONITOR") == 0) {
            uart_link_write_line(wifi_monitor_start() ? "OK" : "FAIL");
        } else if (strcmp(line, "MONITORSTOP") == 0) {
            uart_link_write_line(wifi_monitor_stop() ? "OK" : "FAIL");
        } else if (strcmp(line, "BTSCAN") == 0) {
            uart_link_write_line(bt_scan_start() ? "OK" : "FAIL");
        } else if (strcmp(line, "BTSCANSTOP") == 0) {
            uart_link_write_line(bt_scan_stop() ? "OK" : "FAIL");
        } else if (strcmp(line, "SETUP") == 0) {
            bool ok = wifi_setup_ap_run(SETUP_TIMEOUT_MS);
            uart_link_write_line(ok ? "OK" : "FAIL");
        } else {
            ESP_LOGW(TAG, "Unknown command: %s", line);
        }
    }
}
