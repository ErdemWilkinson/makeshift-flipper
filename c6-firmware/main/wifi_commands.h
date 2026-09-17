#pragma once

#include <stdbool.h>

// Wi-Fi backend for the three commands the P4 sends over UART: SCAN,
// CONNECT, SEND. Each function writes its own reply line(s) via
// uart_link_write_line() and doesn't return a value -- the reply *is*
// the result, same as the wire protocol.

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

// Handles "ASK:<question>": POSTs the question to a local Ollama server
// (see OLLAMA_HOST/OLLAMA_PORT/OLLAMA_MODEL in wifi_commands.c -- the PC
// running Ollama must be on the same network as the C6's STA connection).
// Streams the answer back as one or more "ANSWER:<chunk>" lines (split to
// fit the UART line-length limit), terminated by "ANSWERDONE", or a single
// "ASKFAIL" line if the request couldn't be completed at all.
void wifi_commands_ask(const char *args);
