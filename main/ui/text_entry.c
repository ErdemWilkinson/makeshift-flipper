#include "text_entry.h"

#include "display.h"

// Character grid, laid out in rows. Special cells use multi-char labels;
// everything else is a single printable character appended verbatim.
// Row layout chosen to fit comfortably at 8px/char on the 240px-wide panel
// (up to 24 columns fit with no crowding; we use 10, leaving each cell a
// generous CELL_W_PX so the touch-free joystick cursor is never fiddly to
// land on the right key). GRID_ROWS is 10 (not 6) specifically so this
// covers the full printable-ASCII range WPA/WPA2 passphrases may legally
// use -- see KNOWN_ISSUES.md Round 25: a shorter grid missing uppercase
// K-Z, space, or punctuation like `! # $ % & ' ( ) + , / : ; = > ? [ ] { }
// ~` could make a user's real password impossible to type here, reporting
// a connection failure that isn't actually a wrong password.
#define GRID_COLS 10
#define GRID_ROWS 10

// "^" = OK (submit), "<" = DEL (backspace), "*" = CLR (clear all), "SP" =
// space (the only 2-char label -- space itself renders as nothing on a
// cell, so it needs a visible placeholder; text_entry_handle_button()
// special-cases this exact 2-char string to append ' ' rather than the
// label text). Every other label is exactly one printable character wide.
// CELL_W_PX (24px) fits 3 characters at the font's 8px/char, so "SP" fits
// with room to spare -- unlike a longer placeholder, which would overflow
// into the next cell.
static const char *const s_grid[GRID_ROWS][GRID_COLS] = {
    {"1","2","3","4","5","6","7","8","9","0"},
    {"q","w","e","r","t","y","u","i","o","p"},
    {"a","s","d","f","g","h","j","k","l","-"},
    {"z","x","c","v","b","n","m","_",".","@"},
    {"Q","W","E","R","T","Y","U","I","O","P"},
    {"A","S","D","F","G","H","J","K","L","Z"},
    {"X","C","V","B","N","M","!","#","$","%"},
    {"&","'","(",")","+",",","/",":",";","="},
    {">","?","[","]","{","}","~","\"","\\","`"},
    {"^","<","*","SP","|","","","","",""},
};

// Centre/A selects a cell. On this keyboard only, short RIGHT/LEFT move the
// cursor; the rest of the UI treats RIGHT as enter and LEFT as back.
#define CONTROL_ROW_CELLS 5

#define CELL_W_PX 24
#define CELL_H_PX 16 // matches the 8x16 font's row height
#define CELL_Y0_PX 32 // leave the top 2 rows (32px) for the title/buffer text

// 32px header + GRID_ROWS*CELL_H_PX must fit within the 240px-tall panel.
_Static_assert(CELL_Y0_PX + GRID_ROWS * CELL_H_PX <= DISPLAY_HEIGHT_PX,
               "text entry grid taller than the display");
_Static_assert(GRID_COLS * CELL_W_PX <= DISPLAY_WIDTH_PX,
               "text entry grid wider than the display");

void text_entry_init(text_entry_t *entry)
{
    entry->buffer[0] = '\0';
    entry->length = 0;
    entry->cursor_row = 0;
    entry->cursor_col = 0;
}

static void append_char(text_entry_t *entry, char c)
{
    if (entry->length < TEXT_ENTRY_MAX_LEN) {
        entry->buffer[entry->length++] = c;
        entry->buffer[entry->length] = '\0';
    }
}

static void backspace(text_entry_t *entry)
{
    if (entry->length > 0) {
        entry->buffer[--entry->length] = '\0';
    }
}

bool text_entry_handle_button(text_entry_t *entry, button_id_t button)
{
    switch (button) {
        case BUTTON_UP:
            entry->cursor_row = (entry->cursor_row + GRID_ROWS - 1) % GRID_ROWS;
            if (entry->cursor_row == GRID_ROWS - 1 && entry->cursor_col >= CONTROL_ROW_CELLS) {
                entry->cursor_col = CONTROL_ROW_CELLS - 1;
            }
            return false;
        case BUTTON_DOWN:
            entry->cursor_row = (entry->cursor_row + 1) % GRID_ROWS;
            if (entry->cursor_row == GRID_ROWS - 1 && entry->cursor_col >= CONTROL_ROW_CELLS) {
                entry->cursor_col = CONTROL_ROW_CELLS - 1;
            }
            return false;
        case BUTTON_LEFT:
            if (entry->cursor_row == GRID_ROWS - 1) {
                entry->cursor_col = (entry->cursor_col + CONTROL_ROW_CELLS - 1) % CONTROL_ROW_CELLS;
            } else {
                entry->cursor_col = (entry->cursor_col + GRID_COLS - 1) % GRID_COLS;
            }
            return false;
        case BUTTON_BACK:
            // The caller leaves the editor on B or long LEFT.
            return false;
        case BUTTON_RIGHT:
            entry->cursor_col = (entry->cursor_col + 1) %
                                (entry->cursor_row == GRID_ROWS - 1 ? CONTROL_ROW_CELLS : GRID_COLS);
            return false;
        case BUTTON_PRESS: {
            const char *label = s_grid[entry->cursor_row][entry->cursor_col];
            char c = label[0];
            if (c == '\0') {
                // Unused trailing cell on the control row -- no-op.
            } else if (label[0] == 'S' && label[1] == 'P' && label[2] == '\0') {
                append_char(entry, ' ');
            } else if (c == '^') {
                return true;
            } else if (c == '<') {
                backspace(entry);
            } else if (c == '*') {
                entry->length = 0;
                entry->buffer[0] = '\0';
            } else {
                append_char(entry, c);
            }
            return false;
        }
        default:
            return false;
    }
}

void text_entry_render(const text_entry_t *entry, const char *title, bool mask)
{
    display_clear();
    display_draw_text_centered(0, title, DISPLAY_COLOR_ACCENT);

    if (mask) {
        // Password entry: show '*' per character instead of the real text,
        // so it isn't readable over someone's shoulder while typing.
        char stars[DISPLAY_COLS + 1];
        int n = entry->length;
        if (n > DISPLAY_COLS) {
            n = DISPLAY_COLS;
        }
        for (int i = 0; i < n; i++) {
            stars[i] = '*';
        }
        stars[n] = '\0';
        display_draw_text(1, 0, stars);
    } else {
        // Show the buffer, right-truncated so the most recently typed
        // characters (near the cursor edit point) stay visible.
        const char *shown = entry->buffer;
        int shown_len = entry->length;
        if (shown_len > DISPLAY_COLS) {
            shown += shown_len - DISPLAY_COLS;
        }
        display_draw_text(1, 0, shown);
    }

    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int x = c * CELL_W_PX;
            int y = CELL_Y0_PX + r * CELL_H_PX;
            bool is_cursor = (r == entry->cursor_row && c == entry->cursor_col);
            if (is_cursor) {
                display_fill_rect(x, y, CELL_W_PX, CELL_H_PX, DISPLAY_COLOR_ACCENT);
                display_draw_text_px(x, y, s_grid[r][c], DISPLAY_COLOR_ACCENT_TEXT, DISPLAY_COLOR_ACCENT);
            } else {
                display_draw_text_px(x, y, s_grid[r][c], DISPLAY_COLOR_TEXT, DISPLAY_COLOR_BACKGROUND);
            }
        }
    }

    display_draw_text(13, 0, "YUK/ASA:satir SOL/SAG:sutun");
    display_draw_text(14, 0, "SOL uzun:cik SAG uzun:sec");

    display_flush();
}
