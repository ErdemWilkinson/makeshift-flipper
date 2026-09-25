#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "input/buttons.h"

// Keyboard controls: UP/DOWN change row, short LEFT/RIGHT move across columns,
// A/centre PRESS (or long RIGHT) select a cell, and long LEFT cancels.
// Outside this screen, LEFT backs out and RIGHT enters/selects as before.

#define TEXT_ENTRY_MAX_LEN 63

typedef struct {
    char buffer[TEXT_ENTRY_MAX_LEN + 1];
    int length;
    int cursor_row; // index into the character grid
    int cursor_col;
} text_entry_t;

// Resets to an empty buffer and the grid's top-left cell.
void text_entry_init(text_entry_t *entry);

// Feeds one button event in:
//  - UP/DOWN change row with wraparound
//  - LEFT/RIGHT move one column with wraparound
//  - PRESS activates the selected character or OK/DEL/CLEAR
// Returns true when OK is selected.
bool text_entry_handle_button(text_entry_t *entry, button_id_t button);

// Draws the current buffer plus the character grid with the cursor
// highlighted, and flushes to the display. If `mask` is true, the buffer
// line shows '*' for each character instead of the actual text (use for
// password entry to avoid shoulder-surfing).
void text_entry_render(const text_entry_t *entry, const char *title, bool mask);
