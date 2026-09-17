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

// Handles "DEBUG:<module>|<code>|<note>": POSTs the error report to the
// debug_server.py helper on the PC (see c6-firmware/tools/debug_server.py
// and DEBUG_SERVER_HOST/PORT in wifi_commands.c -- separate from Ollama's
// own port, since this needs the extra "diagnose, then log to a file" step
// that Ollama itself can't do). `note` may be empty. Replies with
// "DIAG:<verdict>|<explanation>" (verdict is "user"/"system"/"unknown") or
// a single "DIAGFAIL" line if the request couldn't be completed.
void wifi_commands_debug(const char *args);
