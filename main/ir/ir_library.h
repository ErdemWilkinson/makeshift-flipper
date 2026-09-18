#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ir_nec.h"

// A small named collection of captured IR codes ("universal remote"
// style), persisted to NVS so codes survive a reboot. Deliberately
// minimal: an ir_nec_frame_t is 2 bytes, so the whole library fits
// comfortably in one NVS blob -- no per-entry keys, no wear-leveling
// concerns beyond what diag.c already established (explicit save, not
// automatic on every capture).

#define IR_LIBRARY_MAX_ENTRIES 16
#define IR_LIBRARY_NAME_MAX_LEN 20 // fits DISPLAY_COLS with room for a cursor/marker

typedef struct {
    char name[IR_LIBRARY_NAME_MAX_LEN + 1];
    ir_nec_frame_t frame;
} ir_library_entry_t;

// Loads the persisted library from NVS into RAM. Call once at startup
// (same pattern as diag_load() -- see main.c's app_main()). A true no-op
// if nothing was ever saved or the saved blob doesn't match this build's
// entry layout: leaves the in-RAM library exactly as it was (empty, if
// called before anything else touches it), same reasoning as diag_load().
void ir_library_load(void);

// Persists the current in-RAM library to NVS, overwriting whatever was
// saved before. Returns false on an NVS write failure -- callers can
// ignore it (the RAM copy is authoritative for the current boot either
// way).
bool ir_library_save(void);

// Appends a new entry (name copied/truncated to IR_LIBRARY_NAME_MAX_LEN,
// caller owns the buffer). Returns false without adding anything if the
// library is already at IR_LIBRARY_MAX_ENTRIES capacity -- callers should
// tell the user to delete something first rather than silently dropping
// the oldest entry (unlike diag.c's ring buffer, losing a saved remote
// code isn't something a user would want to happen automatically).
bool ir_library_add(const char *name, const ir_nec_frame_t *frame);

// Removes the entry at `index` (0-based, matching ir_library_get()'s
// ordering), shifting later entries down by one. No-op if index is out of
// range.
void ir_library_remove(int index);

// Number of entries currently in the library (<= IR_LIBRARY_MAX_ENTRIES).
int ir_library_count(void);

// Returns a pointer to the entry at `index` (0-based, insertion order --
// oldest first, unlike diag.c's newest-first history), or NULL if index
// is out of range. The returned pointer is only valid until the next
// ir_library_add()/ir_library_remove() call.
const ir_library_entry_t *ir_library_get(int index);
