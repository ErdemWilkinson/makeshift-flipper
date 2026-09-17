#pragma once

#include <stdbool.h>
#include <stdint.h>

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

// Max length of an AI answer this side will accumulate (across all
// "ANSWER:" chunks) before truncating -- keeps a runaway/misbehaving
// response from growing the buffer without bound.
#define C6_ASK_ANSWER_MAX_LEN 1024

// Sends ASK:<question> and blocks (up to a minute -- the PC-side LLM does
// the actual generation) waiting for "ANSWER:" chunk lines terminated by
// "ANSWERDONE". Concatenates the chunks into `out_answer` (capacity
// C6_ASK_ANSWER_MAX_LEN + 1, null-terminated). Returns true on success,
// false on timeout, link error, or an "ASKFAIL" reply.
bool c6_link_ask(const char *question, char *out_answer);

// Sends SETUP: tells the C6 to open its web-based Wi-Fi setup AP
// ("MakeshiftFlipper-Setup") and a captive HTTP page at 192.168.4.1, then
// blocks (up to several minutes -- there's a human in the loop) until the
// C6 reports a successful station connection or its own setup timeout
// elapses. Returns true once connected.
bool c6_link_setup(void);

#define C6_DEBUG_VERDICT_MAX_LEN 16
#define C6_DEBUG_EXPLANATION_MAX_LEN 400

typedef struct {
    char verdict[C6_DEBUG_VERDICT_MAX_LEN + 1];         // "user" / "system" / "unknown"
    char explanation[C6_DEBUG_EXPLANATION_MAX_LEN + 1]; // short AI-written explanation, Turkish
} c6_debug_result_t;

// Sends "DEBUG:<module>|<code>|<note>" (see main/diag/diag.h for where
// `module`/`code` come from) and blocks (up to a minute or so -- the
// PC-side debug_server.py runs its own Ollama call) waiting for a
// "DIAG:<verdict>|<explanation>" reply, which it splits into `out_result`.
// `note` may be empty but must not itself contain '|' (the on-device
// scroll keyboard can't produce one, so this isn't a real restriction in
// practice). Returns true on success, false on timeout, link error, or a
// "DIAGFAIL" reply.
bool c6_link_debug(const char *module, const char *code, const char *note,
                    c6_debug_result_t *out_result);
