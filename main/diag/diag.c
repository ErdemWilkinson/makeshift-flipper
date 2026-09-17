#include "diag.h"

#include <string.h>

#include "esp_timer.h"

static diag_entry_t s_history[DIAG_HISTORY_CAPACITY];
static int s_count;      // number of valid entries, caps at DIAG_HISTORY_CAPACITY
static int s_next_slot;  // ring buffer write cursor

void diag_record_error(const char *module, const char *code)
{
    diag_entry_t *e = &s_history[s_next_slot];
    strncpy(e->module, module, DIAG_MODULE_MAX_LEN);
    e->module[DIAG_MODULE_MAX_LEN] = '\0';
    strncpy(e->code, code, DIAG_CODE_MAX_LEN);
    e->code[DIAG_CODE_MAX_LEN] = '\0';
    e->timestamp_us = esp_timer_get_time();

    s_next_slot = (s_next_slot + 1) % DIAG_HISTORY_CAPACITY;
    if (s_count < DIAG_HISTORY_CAPACITY) {
        s_count++;
    }
}

void diag_clear(void)
{
    s_count = 0;
    s_next_slot = 0;
}

int diag_get_history_count(void)
{
    return s_count;
}

int diag_get_history(diag_entry_t *out_entries, int max_entries)
{
    int n = (s_count < max_entries) ? s_count : max_entries;
    // Newest-first: walk backward from the most recently written slot.
    for (int i = 0; i < n; i++) {
        int idx = (s_next_slot - 1 - i + DIAG_HISTORY_CAPACITY) % DIAG_HISTORY_CAPACITY;
        out_entries[i] = s_history[idx];
    }
    return n;
}
