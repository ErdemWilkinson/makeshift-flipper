#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "display.h"

// A read-only, scrollable text viewer for showing a long block of text on
// the small OLED. Wraps the text into DISPLAY_COLS-wide lines once at open
// time, then lets the caller scroll through them with UP/DOWN, same
// button vocabulary as the rest of the UI. BACK (handled by the caller,
// like every other screen) exits back to the menu.
//
// Not currently used by any menu action (its original caller, "Ask AI",
// was removed -- see KNOWN_ISSUES.md) but kept as a generic component for
// whatever next needs to show a long wrapped/scrollable string.

#define ANSWER_VIEW_MAX_LINES 64

typedef struct {
    char lines[ANSWER_VIEW_MAX_LINES][DISPLAY_COLS + 1];
    int line_count;
    int scroll_offset; // index of the first visible line
} answer_view_t;

// Word-wraps `text` into `view`'s line buffer (truncates at
// ANSWER_VIEW_MAX_LINES worth of content if the text is unusually long).
void answer_view_init(answer_view_t *view, const char *title, const char *text);

// UP/DOWN scroll by one line, clamped to the text's extent. Returns true if
// the scroll position changed (caller can skip a redundant redraw otherwise).
bool answer_view_scroll(answer_view_t *view, int delta);

// Draws the title (row 0), as many wrapped lines as fit below it, and a
// scroll indicator, then flushes to the display.
void answer_view_render(const answer_view_t *view, const char *title);
