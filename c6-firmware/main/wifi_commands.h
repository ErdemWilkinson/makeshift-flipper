#pragma once

#include <stdbool.h>

// Wi-Fi backend for the STA-mode commands the P4 sends over UART: SCAN,
// CONNECT, SEND, and the LOGSEND/LOGSENDDONE error-log upload. Each
// function writes its own reply line(s) via uart_link_write_line() and
// doesn't return a value -- the reply *is* the result, same as the wire
// protocol. Wi-Fi Monitor and BT Scan are separate subsystems
// (wifi_monitor.c/bt_scan.c) with their own headers.

// Brings up esp_netif/esp_wifi in STA mode. Call once at startup, after
// nvs_flash_init().
void wifi_commands_init(void);

// Handles "SCAN": scans and writes "NET:<ssid>,<rssi>" for each result,
// then "SCANDONE".
void wifi_commands_scan(void);

// Handles "CONNECT:<ssid>,<password>": connects and writes "OK" or "FAIL".
void wifi_commands_connect(const char *args);

// Core of wifi_commands_connect(), without the UART reply -- shared with
// wifi_setup_ap.c so both paths use the same connect/wait logic. Blocks
// until connected or CONNECT_TIMEOUT_MS elapses. Returns true on success.
bool wifi_commands_connect_sta(const char *ssid, const char *password);

// Handles "SEND:<ip>:<port>:<data>": opens a TCP connection, writes `data`,
// closes it, and writes "SENT" or "FAIL".
void wifi_commands_send(const char *args);

// Handles one "LOGSEND:<json>" line: accumulates `json_line` (one JSON
// object, e.g. {"module":"...","code":"...","ago_s":123}) into the
// current batch. Call once per LOGSEND: line received; the batch is sent
// (and reset) by wifi_commands_log_flush() below. Doesn't write a UART
// reply itself -- only the LOGSENDDONE/flush step does.
void wifi_commands_log_line(const char *json_line);

// Handles "LOGSENDDONE": wraps the batch accumulated via
// wifi_commands_log_line() into {"entries":[...]} and POSTs it to the log
// server helper (see c6-firmware/tools/debug_server.py and
// LOG_SERVER_HOST/PORT in wifi_commands.c). No AI/Ollama involved -- the
// PC side just appends the entries to a log file. Writes "SENT" or "FAIL"
// and resets the batch either way.
void wifi_commands_log_flush(void);
