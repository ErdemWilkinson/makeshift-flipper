// Host test for main/ui/text_entry.c's cursor/buffer logic
// (text_entry_handle_button et al). Compiles the real source directly,
// against tests/stubs/display.h instead of ESP-IDF's display driver --
// text_entry_render() is stubbed out below since it just draws pixels and
// isn't exercised by these tests.

#include <stdbool.h>

#include "minitest.h"

// Stub implementations for the display_* calls text_entry_render() makes.
// text_entry_render() itself is never called by these tests, but the
// translation unit needs these defined to link.
void display_clear(void) {}
void display_draw_text(int row, int col, const char *text) { (void)row; (void)col; (void)text; }
void display_draw_text_px(int x, int y, const char *text, bool invert) { (void)x; (void)y; (void)text; (void)invert; }
void display_fill_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void display_flush(void) {}

#include "../main/ui/text_entry.c"

static text_entry_t new_entry(void)
{
    text_entry_t e;
    text_entry_init(&e);
    return e;
}

MT_TEST(init_starts_empty_at_top_left)
{
    text_entry_t e = new_entry();
    MT_CHECK_EQ_STR(e.buffer, "");
    MT_CHECK_EQ_INT(e.length, 0);
    MT_CHECK_EQ_INT(e.cursor_row, 0);
    MT_CHECK_EQ_INT(e.cursor_col, 0);
}

MT_TEST(press_on_digit_cell_appends_that_character)
{
    text_entry_t e = new_entry();
    // Top-left cell of the grid is '1' (row 0, col 0).
    bool submitted = text_entry_handle_button(&e, BUTTON_PRESS);
    MT_CHECK(!submitted);
    MT_CHECK_EQ_STR(e.buffer, "1");
    MT_CHECK_EQ_INT(e.length, 1);
}

MT_TEST(right_then_press_appends_second_grid_column)
{
    text_entry_t e = new_entry();
    text_entry_handle_button(&e, BUTTON_RIGHT); // move to col 1 ('2')
    text_entry_handle_button(&e, BUTTON_PRESS);
    MT_CHECK_EQ_STR(e.buffer, "2");
}

MT_TEST(down_then_press_appends_second_row_first_column)
{
    text_entry_t e = new_entry();
    text_entry_handle_button(&e, BUTTON_DOWN); // row 1, col 0 == 'q'
    text_entry_handle_button(&e, BUTTON_PRESS);
    MT_CHECK_EQ_STR(e.buffer, "q");
}

MT_TEST(cursor_does_not_move_past_grid_edges)
{
    text_entry_t e = new_entry();
    // Already at top-left; UP and LEFT should be no-ops.
    text_entry_handle_button(&e, BUTTON_UP);
    text_entry_handle_button(&e, BUTTON_LEFT);
    MT_CHECK_EQ_INT(e.cursor_row, 0);
    MT_CHECK_EQ_INT(e.cursor_col, 0);
}

MT_TEST(backspace_cell_removes_last_character)
{
    text_entry_t e = new_entry();
    text_entry_handle_button(&e, BUTTON_PRESS); // types '1'
    text_entry_handle_button(&e, BUTTON_RIGHT); // -> '2'
    text_entry_handle_button(&e, BUTTON_PRESS); // types '2' -> buffer "12"
    MT_CHECK_EQ_STR(e.buffer, "12");

    // Navigate to the DEL cell: row 5, col 1 (grid row {"^","<","*",...}).
    // Cursor is already at col 1 from the "2" entry above; only the row
    // needs to move.
    for (int i = 0; i < 5; i++) text_entry_handle_button(&e, BUTTON_DOWN);
    bool submitted = text_entry_handle_button(&e, BUTTON_PRESS);
    MT_CHECK(!submitted);
    MT_CHECK_EQ_STR(e.buffer, "1");
}

MT_TEST(backspace_on_empty_buffer_is_a_no_op)
{
    text_entry_t e = new_entry();
    for (int i = 0; i < 5; i++) text_entry_handle_button(&e, BUTTON_DOWN);
    text_entry_handle_button(&e, BUTTON_RIGHT); // '<' cell
    text_entry_handle_button(&e, BUTTON_PRESS);
    MT_CHECK_EQ_STR(e.buffer, "");
    MT_CHECK_EQ_INT(e.length, 0);
}

MT_TEST(clear_cell_empties_the_buffer)
{
    text_entry_t e = new_entry();
    text_entry_handle_button(&e, BUTTON_PRESS); // "1"
    for (int i = 0; i < 5; i++) text_entry_handle_button(&e, BUTTON_DOWN);
    for (int i = 0; i < 2; i++) text_entry_handle_button(&e, BUTTON_RIGHT); // col 2 == '*'
    text_entry_handle_button(&e, BUTTON_PRESS);
    MT_CHECK_EQ_STR(e.buffer, "");
}

MT_TEST(ok_cell_returns_true_without_appending)
{
    text_entry_t e = new_entry();
    for (int i = 0; i < 5; i++) text_entry_handle_button(&e, BUTTON_DOWN); // row 5, col 0 == '^'
    bool submitted = text_entry_handle_button(&e, BUTTON_PRESS);
    MT_CHECK(submitted);
    MT_CHECK_EQ_STR(e.buffer, "");
}

MT_TEST(buffer_stops_growing_at_max_len)
{
    text_entry_t e = new_entry();
    // Cursor stays on '1' the whole time (no navigation) -- press it
    // TEXT_ENTRY_MAX_LEN + 5 times and confirm it doesn't overflow.
    for (int i = 0; i < TEXT_ENTRY_MAX_LEN + 5; i++) {
        text_entry_handle_button(&e, BUTTON_PRESS);
    }
    MT_CHECK_EQ_INT(e.length, TEXT_ENTRY_MAX_LEN);
    MT_CHECK_EQ_INT((int)strlen(e.buffer), TEXT_ENTRY_MAX_LEN);
}

int main(void)
{
    printf("test_text_entry:\n");
    MT_RUN(init_starts_empty_at_top_left);
    MT_RUN(press_on_digit_cell_appends_that_character);
    MT_RUN(right_then_press_appends_second_grid_column);
    MT_RUN(down_then_press_appends_second_row_first_column);
    MT_RUN(cursor_does_not_move_past_grid_edges);
    MT_RUN(backspace_cell_removes_last_character);
    MT_RUN(backspace_on_empty_buffer_is_a_no_op);
    MT_RUN(clear_cell_empties_the_buffer);
    MT_RUN(ok_cell_returns_true_without_appending);
    MT_RUN(buffer_stops_growing_at_max_len);
    return MT_SUMMARY();
}
