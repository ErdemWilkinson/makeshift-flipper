#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Waveshare Pico-LCD-1.3 ST7789 SPI LCD, 240x240, RGB565 (65K colors).
// Replaces the earlier
// SSD1306 I2C OLED (128x64, 1-bit) -- see KNOWN_ISSUES.md for the
// migration notes and README.md's pin plan for the new SPI wiring.

#define DISPLAY_WIDTH_PX  240
#define DISPLAY_HEIGHT_PX 240

// 8x16 font (font8x16_basic.h): 240/8 = 30 columns, 240/16 = 15 rows.
#define DISPLAY_ROWS 15
#define DISPLAY_COLS 30

// RGB565: 5 bits red, 6 bits green, 5 bits blue, packed into one uint16_t.
typedef uint16_t display_color_t;

#define DISPLAY_RGB(r, g, b) \
    ((display_color_t)((((r) & 0x1F) << 11) | (((g) & 0x3F) << 5) | ((b) & 0x1F)))

// The device's identity: name shown on the boot splash, the About screen,
// and anywhere else the build wants to brand itself. One place to change
// it for a rebrand/fork.
#define DEVICE_NAME "ErdemFlip"

// The device's color theme -- centralized here (rather than scattered
// display_color_t literals through main.c) so the palette can be reviewed
// or swapped in one place. Values are deliberately high-contrast: this is
// a small 1.3" panel viewed at arm's length, not a phone screen.
//
// Warm red/orange identity (chosen over the earlier green-on-black
// "terminal" look to make this build visually its own): near-black with a
// faint red tint for the background, warm amber body text, and a hot
// red-orange accent for selection/headers -- keeps error red and OK green
// clearly distinct from the accent so status colors still read at a glance.
#define DISPLAY_COLOR_BACKGROUND DISPLAY_RGB(2, 1, 1)       // near-black, faint warm tint
#define DISPLAY_COLOR_TEXT       DISPLAY_RGB(30, 40, 10)    // warm amber
#define DISPLAY_COLOR_ACCENT     DISPLAY_RGB(31, 18, 2)     // hot red-orange -- selection highlight, headers
#define DISPLAY_COLOR_ACCENT_TEXT DISPLAY_RGB(0, 0, 0)      // text drawn on top of an accent-filled area
#define DISPLAY_COLOR_ERROR      DISPLAY_RGB(31, 4, 4)      // red -- error/failure states
#define DISPLAY_COLOR_OK         DISPLAY_RGB(10, 46, 8)     // green -- success/connected states (kept distinct from accent)
#define DISPLAY_COLOR_DIM        DISPLAY_RGB(14, 10, 4)     // dim warm brown -- secondary/disabled text

// Brings up SPI + the ST7789 panel. Call once at startup.
void display_init(void);

// Clears the internal frame buffer to DISPLAY_COLOR_BACKGROUND (does not
// push to the panel yet).
void display_clear(void);

// Draws text at a character cell (row/col), 8x16 font, in `color` on the
// current background. Truncates at DISPLAY_COLS. This is the call most of
// main.c uses; use display_draw_text_color() to pick a color explicitly
// (this is a thin wrapper for DISPLAY_COLOR_TEXT, the common case).
void display_draw_text(int row, int col, const char *text);
void display_draw_text_color(int row, int col, const char *text, display_color_t color);

// Draws text at an arbitrary pixel position (not grid-snapped), in `fg` on
// a `bg`-filled cell background (both explicit -- there is no invert/XOR
// mode on a color panel, unlike the old 1-bit display_draw_text_px()).
// Used by menu.c/text_entry.c for the selection highlight and animations.
void display_draw_text_px(int x, int y, const char *text, display_color_t fg, display_color_t bg);

// Fills a solid rectangle with `color` (used for the selection highlight
// and status bars/badges).
void display_fill_rect(int x, int y, int w, int h, display_color_t color);

// Pushes the internal frame buffer to the physical panel.
void display_flush(void);
