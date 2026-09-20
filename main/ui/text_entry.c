#include "text_entry.h"

#include "display.h"

// Character grid, laid out in rows. Special cells use multi-char labels;
// everything else is a single printable character appended verbatim.
// Row layout chosen to fit comfortably at 8px/char on the 240px-wide panel
// (up to 24 columns fit with no crowding; we use 10, leaving each cell a
// generous CELL_W_PX so the touch-free joystick cursor is never fiddly to
// land on the right key).
#define GRID_COLS 10
#define GRID_ROWS 6

// "^" = OK (submit), "<" = DEL (backspace), "*" = CLR (clear all). Kept to
// one character each so every cell is exactly CELL_W_PX wide with nothing
// overflowing into its neighbor.
static const char *const s_grid[GRID_ROWS][GRID_COLS] = {
    {"1","2","3","4","5","6","7","8","9","0"},
    {"q","w","e","r","t","y","u","i","o","p"},
    {"a","s","d","f","g","h","j","k","l","-"},
    {"z","x","c","v","b","n","m","_",".","@"},
    {"Q","W","E","R","T","Y","U","I","O","P"},
    {"^","<","*","A","S","D","F","G","H","J"},
};

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
            if (entry->cursor_row > 0) {
                entry->cursor_row--;
            }
            return false;
        case BUTTON_DOWN:
            if (entry->cursor_row < GRID_ROWS - 1) {
                entry->cursor_row++;
            }
            return false;
        case BUTTON_LEFT:
            if (entry->cursor_col > 0) {
                entry->cursor_col--;
            }
            return false;
        case BUTTON_RIGHT:
            if (entry->cursor_col < GRID_COLS - 1) {
                entry->cursor_col++;
            }
            return false;
        case BUTTON_PRESS: {
            char c = s_grid[entry->cursor_row][entry->cursor_col][0];
            if (c == '^') {
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
    display_draw_text(0, 0, title);

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

    display_flush();
}
