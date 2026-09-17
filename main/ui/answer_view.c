#include "answer_view.h"

#include <stdbool.h>
#include <string.h>

#include "display.h"

// Room for one row's worth of characters plus the null terminator; matches
// the fixed-width line buffer in answer_view.h.
#define LINE_WIDTH (DISPLAY_COLS)

static void push_line(answer_view_t *view, const char *start, int len)
{
    if (view->line_count >= ANSWER_VIEW_MAX_LINES) {
        return; // silently drop overflow -- see answer_view_init()'s doc comment
    }
    if (len > LINE_WIDTH) {
        len = LINE_WIDTH;
    }
    memcpy(view->lines[view->line_count], start, len);
    view->lines[view->line_count][len] = '\0';
    view->line_count++;
}

void answer_view_init(answer_view_t *view, const char *title, const char *text)
{
    (void)title; // title is drawn by answer_view_render(), not stored here
    view->line_count = 0;
    view->scroll_offset = 0;

    const char *word_start = text;
    char line_buf[LINE_WIDTH + 1];
    int line_len = 0;

    const char *p = text;
    for (;;) {
        bool at_space = (*p == ' ' || *p == '\0');
        if (at_space) {
            int word_len = (int)(p - word_start);
            if (word_len > 0) {
                int sep = (line_len > 0) ? 1 : 0; // room for a joining space
                if (line_len + sep + word_len > LINE_WIDTH) {
                    if (line_len > 0) {
                        push_line(view, line_buf, line_len);
                    }
                    line_len = 0;
                    // A single word longer than the whole line width is
                    // hard-split rather than left to overflow.
                    while (word_len > LINE_WIDTH) {
                        push_line(view, word_start, LINE_WIDTH);
                        word_start += LINE_WIDTH;
                        word_len -= LINE_WIDTH;
                    }
                }
                if (line_len > 0) {
                    line_buf[line_len++] = ' ';
                }
                memcpy(line_buf + line_len, word_start, word_len);
                line_len += word_len;
            }
            if (*p == '\0') {
                break;
            }
            word_start = p + 1;
        }
        p++;
    }
    if (line_len > 0) {
        push_line(view, line_buf, line_len);
    }
    if (view->line_count == 0) {
        push_line(view, "", 0);
    }
}

bool answer_view_scroll(answer_view_t *view, int delta)
{
    int visible_rows = DISPLAY_ROWS - 1; // row 0 is the title
    int max_offset = view->line_count - visible_rows;
    if (max_offset < 0) {
        max_offset = 0;
    }

    int new_offset = view->scroll_offset + delta;
    if (new_offset < 0) {
        new_offset = 0;
    }
    if (new_offset > max_offset) {
        new_offset = max_offset;
    }

    if (new_offset == view->scroll_offset) {
        return false;
    }
    view->scroll_offset = new_offset;
    return true;
}

void answer_view_render(const answer_view_t *view, const char *title)
{
    display_clear();
    display_draw_text(0, 0, title);

    int visible_rows = DISPLAY_ROWS - 1;
    for (int i = 0; i < visible_rows; i++) {
        int line_idx = view->scroll_offset + i;
        if (line_idx >= view->line_count) {
            break;
        }
        display_draw_text(1 + i, 0, view->lines[line_idx]);
    }

    display_flush();
}
