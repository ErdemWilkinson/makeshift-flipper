#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "input/buttons.h"

// A joystick-driven "scroll keyboard": a grid of characters the user
// scrolls through with UP/DOWN/LEFT/RIGHT and picks with PRESS, building up
// a text buffer one character at a time. Meant as the no-phone fallback for
// entering things like a Wi-Fi password -- slower than typing, but needs
// nothing but the joystick already on the board.

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
//  - UP/DOWN/LEFT/RIGHT move the grid cursor
//  - PRESS appends the character under the cursor (or triggers OK/DEL/CLEAR
//    if the cursor is on one of those special cells)
// Returns true if PRESS was on the "OK" cell (caller should treat the
// buffer as finished/submitted).
bool text_entry_handle_button(text_entry_t *entry, button_id_t button);

// Draws the current buffer plus the character grid with the cursor
// highlighted, and flushes to the display. If `mask` is true, the buffer
// line shows '*' for each character instead of the actual text (use for
// password entry to avoid shoulder-surfing).
void text_entry_render(const text_entry_t *entry, const char *title, bool mask);
