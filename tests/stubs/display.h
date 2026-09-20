#pragma once

// Host-test stub for main/ui/display.h. text_entry.c's cursor/buffer logic
// (text_entry_init, text_entry_handle_button) never calls display_*, only
// text_entry_render() does -- but the file includes display.h and calls
// those functions, so the translation unit still needs matching
// declarations to compile and link. Definitions live in test_text_entry.c.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DISPLAY_WIDTH_PX  240
#define DISPLAY_HEIGHT_PX 240
#define DISPLAY_ROWS 15
#define DISPLAY_COLS 30

typedef uint16_t display_color_t;

#define DISPLAY_RGB(r, g, b) \
    ((display_color_t)((((r) & 0x1F) << 11) | (((g) & 0x3F) << 5) | ((b) & 0x1F)))

#define DISPLAY_COLOR_BACKGROUND DISPLAY_RGB(0, 0, 0)
#define DISPLAY_COLOR_TEXT       DISPLAY_RGB(28, 60, 28)
#define DISPLAY_COLOR_ACCENT     DISPLAY_RGB(31, 40, 4)
#define DISPLAY_COLOR_ACCENT_TEXT DISPLAY_RGB(0, 0, 0)
#define DISPLAY_COLOR_ERROR      DISPLAY_RGB(31, 8, 8)
#define DISPLAY_COLOR_OK         DISPLAY_RGB(6, 50, 10)
#define DISPLAY_COLOR_DIM        DISPLAY_RGB(10, 20, 10)

void display_init(void);
void display_clear(void);
void display_draw_text(int row, int col, const char *text);
void display_draw_text_color(int row, int col, const char *text, display_color_t color);
void display_draw_text_px(int x, int y, const char *text, display_color_t fg, display_color_t bg);
void display_fill_rect(int x, int y, int w, int h, display_color_t color);
void display_flush(void);
