#pragma once

#include <stdbool.h>

// Passive Wi-Fi monitor/promiscuous mode: hops channels 1-13 listening for
// beacon/probe-response frames and reports each AP it sees (SSID, BSSID,
// channel, RSSI) back to the P4 as unsolicited "PKT:" lines. No
// deauth/injection -- this only ever receives, never transmits management
// frames of its own.
//
// Separate from wifi_commands.c (STA mode) because promiscuous mode and a
// connected STA don't coexist: starting the monitor disconnects any active
// STA connection (channel-hopping would otherwise fight the STA's own
// channel), and the P4 side is expected to treat SCAN/CONNECT/ASK/DEBUG as
// unavailable while a monitor session is running -- see c6_link.h's
// c6_link_monitor_start() comment.

// Creates the packet queue and starts the (idle until wifi_monitor_start())
// UART TX task. Call once at startup, after wifi_commands_init().
void wifi_monitor_init(void);

// Disconnects STA (if connected), enables promiscuous mode, and starts
// hopping channels 1-13 (~400ms/channel). From this point on, every AP
// seen is pushed to the P4 as its own "PKT:<bssid>,<ssid>,<rssi>,<channel>"
// line via uart_link_write_line(), asynchronously, until
// wifi_monitor_stop() is called. Returns false if promiscuous mode
// couldn't be enabled (rare -- a wrapped esp_wifi_* failure).
bool wifi_monitor_start(void);

// Stops channel-hopping and promiscuous mode. Does NOT automatically
// reconnect STA -- callers that need Wi-Fi back should run WiFi Setup /
// WiFi Setup Manual again. Safe to call even if monitor wasn't running.
bool wifi_monitor_stop(void);
