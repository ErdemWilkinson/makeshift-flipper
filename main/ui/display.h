#pragma once

#include <stdbool.h>
#include <stddef.h>

#define DISPLAY_ROWS 8   // 8px font rows on a 64px-tall OLED
#define DISPLAY_COLS 21  // ~6px-wide font on a 128px-wide OLED
#define DISPLAY_WIDTH_PX  128
#define DISPLAY_HEIGHT_PX 64

// Brings up I2C + the SSD1306 panel. Call once at startup.
void display_init(void);

// Clears the internal frame buffer (does not push to the panel yet).
void display_clear(void);

// Draws text at a character cell (row/col), 8x8 font. Truncates at DISPLAY_COLS.
void display_draw_text(int row, int col, const char *text);

// Draws text at an arbitrary pixel position (not grid-snapped). Used for
// animations (e.g. sliding rows). If `invert` is true, pixels are XORed
// instead of OR'd, so text drawn over a filled rect appears as background-
// colored "holes" (selection highlight look).
void display_draw_text_px(int x, int y, const char *text, bool invert);

// Fills a solid horizontal bar (used for the selection highlight).
void display_fill_rect(int x, int y, int w, int h);

// Pushes the internal frame buffer to the physical panel.
void display_flush(void);
