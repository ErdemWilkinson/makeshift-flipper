#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "diag/diag.h"

// P4-side client for the line-based UART protocol spoken to the ESP32-C6
// companion radio (see c6-firmware/ for the other end). Every command is
// one newline-terminated ASCII line out, and one newline-terminated ASCII
// line back:
//
//   SCAN                        -> zero or more "NET:<ssid>,<rssi>",
//                                   terminated by "SCANDONE"
//   CONNECT:<ssid>,<password>   -> "OK" or "FAIL"
//   SEND:<ip>:<port>:<data>     -> "SENT" or "FAIL"

#define C6_MAX_NETWORKS 16
#define C6_SSID_MAX_LEN 32

typedef struct {
    char ssid[C6_SSID_MAX_LEN + 1];
    int8_t rssi;
} c6_network_t;

// Starts the UART link to the C6. Call once at startup.
void c6_link_init(void);

// Sends SCAN and blocks until SCANDONE or timeout. Fills `out_networks`
// (capacity `max_networks`) and returns how many were found. Returns -1 on
// timeout/link error.
int c6_link_scan(c6_network_t *out_networks, int max_networks);

// Sends CONNECT:<ssid>,<password> and blocks for the result.
bool c6_link_connect(const char *ssid, const char *password);

// Sends SEND:<ip>:<port>:<data> and blocks for the result.
bool c6_link_send(const char *ip, uint16_t port, const char *data);

// Sends SETUP: tells the C6 to open its web-based Wi-Fi setup AP
// ("MakeshiftFlipper-Setup") and a captive HTTP page at 192.168.4.1, then
// blocks (up to several minutes -- there's a human in the loop) until the
// C6 reports a successful station connection or its own setup timeout
// elapses. Returns true once connected.
bool c6_link_setup(void);

// Uploads the device's local error history (main/diag/diag.h) to a PC-side
// log collector for safekeeping/inspection -- entirely optional, the
// history works fully offline without this. Sends each entry as its own
// "LOGSEND:<json>" line (newline-delimited JSON, since the whole history
// won't fit in one UART line), followed by "LOGSENDDONE", then blocks for
// a single "SENT"/"FAIL" reply once the C6 has POSTed the batch to
// debug_server.py (see c6-firmware/README.md). No AI/Ollama involved on
// either end -- the PC side just writes the entries to a log file.
#define C6_LOGSEND_TIMEOUT_MS (15 * 1000) // local HTTP POST only, no LLM call to wait on
bool c6_link_send_error_log(const diag_entry_t *entries, int count);

// --- Wi-Fi Monitor (passive promiscuous sniffing) --------------------------
// Unlike every API above, this isn't a single request/reply: once started,
// the C6 pushes an unsolicited "PKT:<bssid>,<ssid>,<rssi>,<channel>" line
// for every beacon/probe-response it sees, asynchronously, until stopped.
// c6_link_monitor_start() spins up a dedicated background task that holds
// s_link_mutex for the whole session (see c6_link.c) so it can keep
// reading those lines as they arrive -- which means every OTHER c6_link_*
// call (scan/connect/send/setup/send_error_log) blocks until
// c6_link_monitor_stop() is called. Callers should treat "Wi-Fi Monitor is
// running" as "no other C6 feature is available" and make that visible in
// the UI.
//
// Also disconnects any active STA connection on the C6 side (channel-
// hopping and a connected STA can't share the radio) and does not
// reconnect automatically when stopped -- WiFi Setup/WiFi Setup Manual
// needs to be run again afterward if Wi-Fi is needed.

#define C6_MONITOR_MAX_APS 32
#define C6_MONITOR_SSID_MAX_LEN 32

typedef struct {
    uint8_t bssid[6];
    char ssid[C6_MONITOR_SSID_MAX_LEN + 1];
    int8_t rssi;
    uint8_t channel;
} c6_monitor_ap_t;

// Sends "MONITOR" and blocks briefly for "OK"/"FAIL", then starts the
// background task that collects "PKT:" lines. Returns false if the C6
// couldn't enable promiscuous mode, or if a monitor session is already
// running.
bool c6_link_monitor_start(void);

// Sends "MONITORSTOP", blocks briefly for "OK"/"FAIL", and stops the
// background collector task, releasing s_link_mutex back to normal use.
// Safe to call even if monitor isn't running.
bool c6_link_monitor_stop(void);

// Non-blocking: copies the current set of APs seen so far this session
// into `out_aps` (capacity `max_aps`) and returns how many. Callers poll
// this repeatedly (e.g. every UI tick) while a monitor screen is open.
// The list is deduplicated by BSSID (a later sighting updates rssi/channel
// in place rather than adding a new entry) and capped at
// C6_MONITOR_MAX_APS -- further new BSSIDs past that are dropped.
int c6_link_monitor_poll(c6_monitor_ap_t *out_aps, int max_aps);

// --- BT Scan (passive BLE advertisement scan) -------------------------
// Same asynchronous-push shape as Wi-Fi Monitor above ("BTDEV:<addr>,
// <name>,<rssi>" lines, pushed continuously once started), and the same
// "holds s_link_mutex for the whole session" implementation, for the same
// reason: the UART link is one shared channel, so unsolicited BTDEV lines
// and a ordinary command's reply can't be allowed to interleave, and this
// codebase's wire protocol has no in-band way to multiplex two logical
// streams. Every other c6_link_* call blocks while a BT scan is running,
// same as during Wi-Fi Monitor -- make that visible in the UI.
//
// Unlike Wi-Fi Monitor, this does NOT touch the Wi-Fi radio or the STA
// connection at all -- BLE and Wi-Fi are separate radios on the C6 (with
// standard coexistence handled by the IDF/controller), so e.g. sending the
// error log would be functionally fine to run concurrently with a BT scan
// if the UART weren't the bottleneck. It's the shared UART link, not the
// radio, that forces the same "one feature at a time" rule here.
//
// The C6 only has a BLE radio, no classic BT/BR-EDR -- this only sees
// BLE-advertising devices (BLE headphones/trackers/etc, not classic
// Bluetooth-only gadgets), and it only ever listens (passive scan, no
// connection) -- see c6-firmware/main/bt_scan.c.

#define C6_BT_MAX_DEVICES 32
#define C6_BT_NAME_MAX_LEN 31

typedef struct {
    uint8_t addr[6];
    char name[C6_BT_NAME_MAX_LEN + 1]; // empty if the device didn't advertise one
    int8_t rssi;
} c6_bt_device_t;

// Sends "BTSCAN" and blocks briefly for "OK"/"FAIL", then starts the
// background task that collects "BTDEV:" lines. Returns false if the C6
// couldn't start scanning, or if a BT scan (or Wi-Fi Monitor -- they
// share the same background-task/mutex slot) is already running.
bool c6_link_bt_scan_start(void);

// Sends "BTSCANSTOP", blocks briefly for "OK"/"FAIL", and stops the
// background collector task. Safe to call even if a scan isn't running.
bool c6_link_bt_scan_stop(void);

// Non-blocking, same polling model as c6_link_monitor_poll(): copies the
// current set of devices seen so far into `out_devices` (capacity
// `max_devices`), deduplicated by address, capped at C6_BT_MAX_DEVICES.
int c6_link_bt_scan_poll(c6_bt_device_t *out_devices, int max_devices);
