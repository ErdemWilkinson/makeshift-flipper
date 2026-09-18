#pragma once

// Host-test stub for main/ui/display.h. text_entry.c's cursor/buffer logic
// (text_entry_init, text_entry_handle_button) never calls display_*, only
// text_entry_render() does -- but the file includes display.h and calls
// those functions, so the translation unit still needs matching
// declarations to compile and link. Definitions live in test_text_entry.c.

#include <stdbool.h>
#include <stddef.h>

#define DISPLAY_ROWS 8
#define DISPLAY_COLS 21
#define DISPLAY_WIDTH_PX  128
#define DISPLAY_HEIGHT_PX 64

void display_init(void);
void display_clear(void);
void display_draw_text(int row, int col, const char *text);
void display_draw_text_px(int x, int y, const char *text, bool invert);
void display_fill_rect(int x, int y, int w, int h);
void display_flush(void);
