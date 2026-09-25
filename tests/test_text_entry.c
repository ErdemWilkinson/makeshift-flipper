// Host test for main/ui/text_entry.c's cursor/buffer logic
// (text_entry_handle_button et al). Compiles the real source directly,
// against tests/stubs/display.h instead of ESP-IDF's display driver --
// text_entry_render() is stubbed out below since it just draws pixels and
// isn't exercised by these tests.

#include <stdbool.h>

#include "minitest.h"
#include "display.h" // tests/stubs/display.h -- needed here for display_color_t before the stub fns below

// Stub implementations for the display_* calls text_entry_render() makes.
// text_entry_render() itself is never called by these tests, but the
// translation unit needs these defined to link.
void display_clear(void) {}
void display_draw_text(int row, int col, const char *text) { (void)row; (void)col; (void)text; }
void display_draw_text_color(int row, int col, const char *text, display_color_t color) { (void)row; (void)col; (void)text; (void)color; }
void display_draw_text_centered(int row, const char *text, display_color_t color) { (void)row; (void)text; (void)color; }
void display_draw_text_px(int x, int y, const char *text, display_color_t fg, display_color_t bg) { (void)x; (void)y; (void)text; (void)fg; (void)bg; }
void display_fill_rect(int x, int y, int w, int h, display_color_t color) { (void)x; (void)y; (void)w; (void)h; (void)color; }
void display_flush(void) {}

#include "../main/ui/text_entry.c"

static text_entry_t new_entry(void)
{
    text_entry_t e;
    text_entry_init(&e);
    return e;
}

static bool select_cell(text_entry_t *entry, int row, int col)
{
    int rows = (row - entry->cursor_row + GRID_ROWS) % GRID_ROWS;
    for (int i = 0; i < rows; i++) {
        text_entry_handle_button(entry, BUTTON_DOWN);
    }
    int width = row == GRID_ROWS - 1 ? CONTROL_ROW_CELLS : GRID_COLS;
    int columns = (col - entry->cursor_col + width) % width;
    for (int i = 0; i < columns; i++) {
        text_entry_handle_button(entry, BUTTON_RIGHT);
    }
    return text_entry_handle_button(entry, BUTTON_PRESS);
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

MT_TEST(right_moves_across_and_press_selects)
{
    text_entry_t e = new_entry();
    text_entry_handle_button(&e, BUTTON_RIGHT); // move to '2'
    MT_CHECK_EQ_STR(e.buffer, "");
    text_entry_handle_button(&e, BUTTON_PRESS); // select
    MT_CHECK_EQ_STR(e.buffer, "2");
}

MT_TEST(left_moves_across_and_wraps)
{
    text_entry_t e = new_entry();
    text_entry_handle_button(&e, BUTTON_LEFT);
    MT_CHECK_EQ_INT(e.cursor_col, GRID_COLS - 1);
    text_entry_handle_button(&e, BUTTON_RIGHT);
    MT_CHECK_EQ_INT(e.cursor_col, 0);
    text_entry_handle_button(&e, BUTTON_UP);
    MT_CHECK_EQ_INT(e.cursor_row, GRID_ROWS - 1);
    text_entry_handle_button(&e, BUTTON_LEFT);
    MT_CHECK_EQ_INT(e.cursor_col, CONTROL_ROW_CELLS - 1);
}

MT_TEST(down_moves_to_next_row)
{
    text_entry_t e = new_entry();
    text_entry_handle_button(&e, BUTTON_DOWN);
    text_entry_handle_button(&e, BUTTON_PRESS); // 'q'
    MT_CHECK_EQ_STR(e.buffer, "q");
}

MT_TEST(cursor_wraps_rows_and_control_row_columns)
{
    text_entry_t e = new_entry();
    text_entry_handle_button(&e, BUTTON_UP);
    MT_CHECK_EQ_INT(e.cursor_row, GRID_ROWS - 1);
    MT_CHECK_EQ_INT(e.cursor_col, 0);
    for (int i = 0; i < CONTROL_ROW_CELLS; i++) {
        text_entry_handle_button(&e, BUTTON_RIGHT);
    }
    MT_CHECK_EQ_INT(e.cursor_col, 0);
    text_entry_handle_button(&e, BUTTON_DOWN);
    MT_CHECK_EQ_INT(e.cursor_row, 0);
    MT_CHECK_EQ_INT(e.cursor_col, 0);
}

MT_TEST(control_row_clamps_column_on_vertical_move)
{
    text_entry_t e = new_entry();
    select_cell(&e, 8, 8); // selects a printable character
    text_entry_handle_button(&e, BUTTON_DOWN);
    MT_CHECK_EQ_INT(e.cursor_row, 9);
    MT_CHECK_EQ_INT(e.cursor_col, 4);
}

MT_TEST(backspace_cell_removes_last_character)
{
    text_entry_t e = new_entry();
    select_cell(&e, 0, 0);
    select_cell(&e, 0, 1);
    MT_CHECK_EQ_STR(e.buffer, "12");
    bool submitted = select_cell(&e, 9, 1); // DEL
    MT_CHECK(!submitted);
    MT_CHECK_EQ_STR(e.buffer, "1");
}

MT_TEST(backspace_on_empty_buffer_is_a_no_op)
{
    text_entry_t e = new_entry();
    select_cell(&e, 9, 1);
    MT_CHECK_EQ_STR(e.buffer, "");
    MT_CHECK_EQ_INT(e.length, 0);
}

MT_TEST(clear_cell_empties_the_buffer)
{
    text_entry_t e = new_entry();
    select_cell(&e, 0, 0);
    select_cell(&e, 9, 2); // CLR
    MT_CHECK_EQ_STR(e.buffer, "");
}

MT_TEST(ok_cell_returns_true_without_appending)
{
    text_entry_t e = new_entry();
    bool submitted = select_cell(&e, 9, 0); // OK
    MT_CHECK(submitted);
    MT_CHECK_EQ_STR(e.buffer, "");
}

MT_TEST(space_cell_appends_a_space_character)
{
    text_entry_t e = new_entry();
    select_cell(&e, 9, 3); // SP
    MT_CHECK_EQ_STR(e.buffer, " ");
    MT_CHECK_EQ_INT(e.length, 1);
}

MT_TEST(back_is_not_an_edit_action)
{
    text_entry_t e = new_entry();
    bool submitted = text_entry_handle_button(&e, BUTTON_BACK);
    MT_CHECK(!submitted);
    MT_CHECK_EQ_STR(e.buffer, "");
    MT_CHECK_EQ_INT(e.length, 0);
}

MT_TEST(uppercase_k_through_z_and_punctuation_are_reachable)
{
    text_entry_t e1 = new_entry();
    select_cell(&e1, 5, 7);
    MT_CHECK_EQ_STR(e1.buffer, "K");

    text_entry_t e2 = new_entry();
    select_cell(&e2, 8, 6);
    MT_CHECK_EQ_STR(e2.buffer, "~");
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
    MT_RUN(right_moves_across_and_press_selects);
    MT_RUN(left_moves_across_and_wraps);
    MT_RUN(down_moves_to_next_row);
    MT_RUN(cursor_wraps_rows_and_control_row_columns);
    MT_RUN(control_row_clamps_column_on_vertical_move);
    MT_RUN(backspace_cell_removes_last_character);
    MT_RUN(backspace_on_empty_buffer_is_a_no_op);
    MT_RUN(clear_cell_empties_the_buffer);
    MT_RUN(ok_cell_returns_true_without_appending);
    MT_RUN(space_cell_appends_a_space_character);
    MT_RUN(back_is_not_an_edit_action);
    MT_RUN(uppercase_k_through_z_and_punctuation_are_reachable);
    MT_RUN(buffer_stops_growing_at_max_len);
    return MT_SUMMARY();
}
