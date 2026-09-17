#pragma once

#include <stdbool.h>
#include <stdint.h>

// Tracks the single most recent error/failure the device has seen, across
// every module (RFID/NFC, IR, Wi-Fi/C6 link), so the automatic "Debug AI"
// background task (main.c) can pick it up and send it off for analysis
// without every call site having to pass error details through several
// layers of blocking action functions.
//
// This is deliberately a single global slot, not a history/log: the use
// case is "something just went wrong, get an AI opinion on it," not
// long-term diagnostics. Overwritten by the next recorded error -- if two
// errors happen close together before the first one's report finishes
// sending, only the newer one gets reported (see main.c's background task
// for how `seq` is used to detect "a new error arrived").

#define DIAG_CODE_MAX_LEN 32
#define DIAG_MODULE_MAX_LEN 24

typedef struct {
    char code[DIAG_CODE_MAX_LEN + 1];     // e.g. "RC522_SCAN_ERROR", "ASKFAIL"
    char module[DIAG_MODULE_MAX_LEN + 1]; // e.g. "13.56MHz NFC", "WiFi Setup"
    bool has_error;
    // Incremented on every diag_record_error() call, never reset. Lets a
    // poller detect "a new error arrived since I last checked" by
    // comparing against a saved value, without needing its own separate
    // dirty flag or risking missing an error that arrives and is
    // overwritten between two polls (the seq will have visibly jumped by
    // more than 1, at least signaling something was missed).
    uint32_t seq;
} diag_state_t;

// Records the most recent error, overwriting whatever was there before,
// and bumps `seq`. `code` and `module` are copied (truncated to the max
// lengths above), so callers can pass string literals or stack buffers
// safely.
void diag_record_error(const char *module, const char *code);

// Clears the recorded error (e.g. after a successful retry) -- also bumps
// `seq` so a poller doesn't mistake this for "no change."
void diag_clear(void);

// Returns a pointer to the current state (module/code/has_error/seq).
// Owned by diag.c -- valid until the next diag_record_error()/diag_clear()
// call, so callers that hand this off across a blocking operation (like
// the background debug task) should copy the fields they need first.
const diag_state_t *diag_get(void);
