#pragma once

#include <stdbool.h>
#include <stdint.h>

// Tracks a rolling history of the errors/failures the device has seen,
// across every module (RFID/NFC, IR, Wi-Fi/C6 link), so they can be
// browsed on-device ("Errors" menu, main.c's action_error_history()) and
// optionally uploaded to a PC for safekeeping (c6_link_send_error_log()) --
// entirely offline otherwise, no network/AI dependency.
//
// Fixed-size ring buffer, newest DIAG_HISTORY_CAPACITY entries kept,
// oldest silently dropped once full. Lives in RAM during normal operation
// (diag_record_error() never touches flash, so a burst of failures -- e.g.
// repeated failed RFID reads -- doesn't wear it out) and is persisted to
// NVS only on an explicit diag_save()/diag_load(), which main.c calls
// around the "Errors" screen (load on open, save on exit) and once at
// boot. A crash or battery pull between saves loses whatever happened
// since the last one -- this is "what went wrong recently," not a
// guaranteed-durable audit log.

#define DIAG_CODE_MAX_LEN 32
#define DIAG_MODULE_MAX_LEN 24
#define DIAG_HISTORY_CAPACITY 24

typedef struct {
    char code[DIAG_CODE_MAX_LEN + 1];     // e.g. "RC522_SCAN_ERROR"
    char module[DIAG_MODULE_MAX_LEN + 1]; // e.g. "13.56MHz NFC", "WiFi Setup"
    // esp_timer_get_time() at record time -- microseconds since boot, NOT
    // wall-clock time (no RTC/NTP on this device). Only meaningful as a
    // relative "how long ago" within the current boot session; see
    // KNOWN_ISSUES.md. Entries loaded from a previous boot (via
    // diag_load()) keep whatever timestamp they were saved with, which is
    // relative to THAT boot, not this one -- their displayed "age" will be
    // wrong after a reboot. Accepted limitation, see diag_load()'s comment.
    int64_t timestamp_us;
} diag_entry_t;

// Appends a new entry to the history (oldest is overwritten once
// DIAG_HISTORY_CAPACITY is reached). `code`/`module` are copied
// (truncated to the max lengths above), so callers can pass string
// literals or stack buffers safely. RAM-only -- does not touch NVS.
void diag_record_error(const char *module, const char *code);

// Clears the entire history (RAM only -- call diag_save() afterward to
// also clear the persisted copy, otherwise the next diag_load() brings
// the old entries back).
void diag_clear(void);

// Copies up to `max_entries` entries into `out_entries`, newest first.
// Returns the number actually copied (<= diag_get_history_count()).
int diag_get_history(diag_entry_t *out_entries, int max_entries);

// Number of entries currently recorded (<= DIAG_HISTORY_CAPACITY).
int diag_get_history_count(void);

// --- Persistence (NVS) ------------------------------------------------
// Explicit, not automatic: diag_record_error() itself never writes to
// flash, so callers decide when a save is worth the flash-wear cost (main
// only calls these around the "Errors" menu and once at boot -- see
// action_error_history()/app_main()).

// Loads the persisted history from NVS into RAM, replacing whatever's
// currently there (so call this before any diag_record_error() calls you
// want to keep -- typically once, near the start of app_main(), before
// anything else has a chance to record an error). If nothing was ever
// saved, or the saved blob doesn't match this build's diag_entry_t layout
// (e.g. after a firmware update that changes DIAG_HISTORY_CAPACITY or the
// struct fields), this is a true no-op -- the RAM history is left exactly
// as it was, not cleared -- since "nothing to load" shouldn't destroy
// whatever's already in RAM if this is ever called somewhere other than
// right at boot.
void diag_load(void);

// Persists the current RAM history to NVS, overwriting whatever was saved
// before. Returns false if the NVS write failed (out of space, no NVS
// partition, etc.) -- callers can ignore the failure (the RAM copy is
// unaffected either way) or surface it, main.c's action_error_history()
// just ignores it since the on-screen history is already accurate
// regardless of whether the save succeeded.
bool diag_save(void);
