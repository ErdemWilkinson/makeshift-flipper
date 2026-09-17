#pragma once

#include <stdbool.h>
#include <stdint.h>

// Tracks a rolling history of the errors/failures the device has seen,
// across every module (RFID/NFC, IR, Wi-Fi/C6 link), so they can be
// browsed on-device ("Errors" menu, main.c's action_error_history()) and
// optionally uploaded to a PC for safekeeping (c6_link_send_error_log()) --
// entirely offline otherwise, no network/AI dependency.
//
// Fixed-size ring buffer in RAM: the newest DIAG_HISTORY_CAPACITY entries
// are kept, oldest silently dropped once full. Not persisted across a
// reboot (see KNOWN_ISSUES.md) -- this is meant for "what went wrong this
// session," not a permanent audit log.

#define DIAG_CODE_MAX_LEN 32
#define DIAG_MODULE_MAX_LEN 24
#define DIAG_HISTORY_CAPACITY 24

typedef struct {
    char code[DIAG_CODE_MAX_LEN + 1];     // e.g. "RC522_SCAN_ERROR"
    char module[DIAG_MODULE_MAX_LEN + 1]; // e.g. "13.56MHz NFC", "WiFi Setup"
    // esp_timer_get_time() at record time -- microseconds since boot, NOT
    // wall-clock time (no RTC/NTP on this device). Only meaningful as a
    // relative "how long ago" within the current boot session; see
    // KNOWN_ISSUES.md.
    int64_t timestamp_us;
} diag_entry_t;

// Appends a new entry to the history (oldest is overwritten once
// DIAG_HISTORY_CAPACITY is reached). `code`/`module` are copied
// (truncated to the max lengths above), so callers can pass string
// literals or stack buffers safely.
void diag_record_error(const char *module, const char *code);

// Clears the entire history.
void diag_clear(void);

// Copies up to `max_entries` entries into `out_entries`, newest first.
// Returns the number actually copied (<= diag_get_history_count()).
int diag_get_history(diag_entry_t *out_entries, int max_entries);

// Number of entries currently recorded (<= DIAG_HISTORY_CAPACITY).
int diag_get_history_count(void);
