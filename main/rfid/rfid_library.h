#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rc522.h"
#include "rdm6300.h"

// A small named collection of scanned RFID/NFC tag UIDs ("keychain"
// style), persisted to NVS so entries survive a reboot. Deliberately
// UID-only: unlike main/ir/ir_library.h's full IR codes, this never stores
// a Mifare Classic sector dump (keys, block data) -- only the identifier a
// reader reports, which is what's actually needed to recognize a tag again
// (e.g. "is this the same fob") and carries far less risk if the saved
// library were ever copied off the device than a full clonable dump would.

#define RFID_LIBRARY_MAX_ENTRIES 16
#define RFID_LIBRARY_NAME_MAX_LEN 20 // fits DISPLAY_COLS with room for a cursor/marker

typedef enum {
    RFID_LIBRARY_KIND_125KHZ,   // RDM6300, rdm6300_id_t (5 bytes)
    RFID_LIBRARY_KIND_1356MHZ,  // RC522, rc522_uid_t (4/7/10 bytes, .length used)
} rfid_library_kind_t;

typedef struct {
    char name[RFID_LIBRARY_NAME_MAX_LEN + 1];
    rfid_library_kind_t kind;
    union {
        rdm6300_id_t id_125khz;
        rc522_uid_t uid_1356mhz;
    } u;
} rfid_library_entry_t;

// Loads the persisted library from NVS into RAM. Call once at startup
// (same pattern as diag_load()/ir_library_load() -- see main.c's
// app_main()). A true no-op if nothing was ever saved or the saved blob
// doesn't match this build's entry layout: leaves the in-RAM library
// exactly as it was (empty, if called before anything else touches it).
void rfid_library_load(void);

// Persists the current in-RAM library to NVS, overwriting whatever was
// saved before. Returns false on an NVS write failure -- callers can
// ignore it (the RAM copy is authoritative for the current boot either
// way).
bool rfid_library_save(void);

// Appends a new 125kHz (RDM6300) entry. Returns false without adding
// anything if the library is already at RFID_LIBRARY_MAX_ENTRIES capacity.
bool rfid_library_add_125khz(const char *name, const rdm6300_id_t *id);

// Appends a new 13.56MHz (RC522) entry. Returns false without adding
// anything if the library is already at RFID_LIBRARY_MAX_ENTRIES capacity.
bool rfid_library_add_1356mhz(const char *name, const rc522_uid_t *uid);

// Removes the entry at `index` (0-based, matching rfid_library_get()'s
// ordering), shifting later entries down by one. No-op if index is out of
// range.
void rfid_library_remove(int index);

// Number of entries currently in the library (<= RFID_LIBRARY_MAX_ENTRIES).
int rfid_library_count(void);

// Returns a pointer to the entry at `index` (0-based, insertion order --
// oldest first), or NULL if index is out of range. The returned pointer is
// only valid until the next rfid_library_add_*()/rfid_library_remove() call.
const rfid_library_entry_t *rfid_library_get(int index);

// True if `id` matches an already-saved 125kHz entry's UID (byte-for-byte).
// Used so a scan screen can recognize a known tag without the user manually
// browsing the library.
bool rfid_library_matches_125khz(const rdm6300_id_t *id);

// True if `uid` matches an already-saved 13.56MHz entry's UID (same length
// and bytes). Used the same way as rfid_library_matches_125khz().
bool rfid_library_matches_1356mhz(const rc522_uid_t *uid);
