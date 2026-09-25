// Host test for main/ui/menu.c's navigation logic (menu_handle_button).

#include <stdbool.h>
#include <string.h>

#include "minitest.h"
#include "display.h"

static int g_banner_row;
static int g_banner_col;
static int g_selected_y;
static display_color_t g_selected_color;
static display_color_t g_last_text_color;
static char g_last_label[DISPLAY_COLS * 4 + 1];

void display_clear(void) {}
void display_draw_text(int row, int col, const char *text) { (void)row; (void)col; (void)text; }
void display_draw_text_color(int row, int col, const char *text, display_color_t color)
{
    (void)color;
    if (strcmp(text, "Makeshift Flipper") == 0 || strcmp(text, "HACKING") == 0) {
        g_banner_row = row;
        g_banner_col = col;
    }
}
void display_draw_text_px(int x, int y, const char *text, display_color_t fg, display_color_t bg)
{
    (void)x; (void)y; (void)bg;
    g_last_text_color = fg;
    strncpy(g_last_label, text, sizeof(g_last_label) - 1);
    g_last_label[sizeof(g_last_label) - 1] = '\0';
}
void display_fill_rect(int x, int y, int w, int h, display_color_t color)
{
    (void)x; (void)w; (void)h;
    g_selected_y = y;
    g_selected_color = color;
}
display_color_t display_get_background(void) { return DISPLAY_COLOR_BACKGROUND; }
void display_flush(void) {}

#include "../main/ui/menu.c"

static int g_leaf_calls = 0;
static void leaf_action(void) { g_leaf_calls++; }

static menu_item_t s_root_items[] = {
    {"Category", NULL, NULL},
    {"Leaf", leaf_action, NULL},
};
static menu_item_t s_child_items[] = {
    {"Child A", leaf_action, NULL},
    {"Child B", leaf_action, NULL},
};
static menu_t s_root;
static menu_t s_child;

static void setup(void)
{
    menu_init(&s_root, s_root_items, 2);
    menu_init(&s_child, s_child_items, 2);
    menu_link_submenu(&s_root, &s_root_items[0], &s_child);
    g_leaf_calls = 0;
    g_banner_row = -1;
    g_banner_col = -1;
    g_selected_y = -1;
    g_last_text_color = 0;
    g_last_label[0] = '\0';
}

MT_TEST(banner_has_own_row_and_first_item_remains_selectable)
{
    setup();
    menu_set_banner(&s_root, "Makeshift Flipper");
    s_root.anim_offset_px = 0;
    menu_render(&s_root);
    MT_CHECK_EQ_INT(g_banner_row, 0);
    MT_CHECK_EQ_INT(g_banner_col, 6);
    MT_CHECK_EQ_INT(g_selected_y, 16);
    MT_CHECK(menu_handle_button(&s_root, BUTTON_PRESS) == &s_child);
}

MT_TEST(accent_is_local_to_one_menu)
{
    setup();
    menu_set_accent(&s_child, DISPLAY_COLOR_HACKING_ACCENT);
    s_child.anim_offset_px = 0;
    menu_render(&s_child);
    MT_CHECK_EQ_INT(g_selected_color, DISPLAY_COLOR_HACKING_ACCENT);
    MT_CHECK_EQ_INT(s_root.accent_color, DISPLAY_COLOR_ACCENT);
}

MT_TEST(hacking_menu_uses_red_text_without_filled_row)
{
    setup();
    menu_set_banner(&s_child, "HACKING");
    menu_set_text_style(&s_child, DISPLAY_COLOR_HACKING_TEXT,
                        DISPLAY_COLOR_HACKING_SELECTED, false);
    s_child.anim_offset_px = 0;
    menu_render(&s_child);
    MT_CHECK_EQ_INT(g_banner_col, 11);
    MT_CHECK_EQ_INT(g_selected_y, -1);
    MT_CHECK_EQ_INT(g_last_text_color, DISPLAY_COLOR_HACKING_TEXT);

    menu_t one;
    menu_init(&one, s_child_items, 1);
    menu_set_text_style(&one, DISPLAY_COLOR_HACKING_TEXT,
                        DISPLAY_COLOR_HACKING_SELECTED, false);
    one.anim_offset_px = 0;
    menu_render(&one);
    MT_CHECK_EQ_INT(g_last_text_color, DISPLAY_COLOR_HACKING_SELECTED);
}

MT_TEST(press_on_category_enters_submenu)
{
    setup();
    MT_CHECK(menu_handle_button(&s_root, BUTTON_PRESS) == &s_child);
}

MT_TEST(left_returns_to_parent)
{
    setup();
    MT_CHECK(menu_handle_button(&s_child, BUTTON_LEFT) == &s_root);
}

MT_TEST(back_button_returns_to_parent)
{
    setup();
    MT_CHECK(menu_handle_button(&s_child, BUTTON_BACK) == &s_root);
}

MT_TEST(back_at_top_level_stays_put)
{
    setup();
    MT_CHECK(menu_handle_button(&s_root, BUTTON_BACK) == &s_root);
    MT_CHECK_EQ_INT(g_leaf_calls, 0);
}

MT_TEST(back_does_not_change_selection_of_left_menu)
{
    setup();
    menu_handle_button(&s_child, BUTTON_DOWN);
    menu_handle_button(&s_child, BUTTON_BACK);
    MT_CHECK_EQ_INT(s_child.selected_index, 1);
    MT_CHECK_EQ_INT(s_root.selected_index, 0);
}

MT_TEST(press_on_leaf_runs_action)
{
    setup();
    menu_handle_button(&s_root, BUTTON_DOWN);
    MT_CHECK(menu_handle_button(&s_root, BUTTON_PRESS) == &s_root);
    MT_CHECK_EQ_INT(g_leaf_calls, 1);
}

MT_TEST(turkish_menu_label_keeps_utf8_characters)
{
    setup();
    s_root_items[0].label = "Kızılötesi";
    s_root.anim_offset_px = 0;
    menu_render(&s_root);
    MT_CHECK(strcmp(g_last_label, " Leaf") == 0);
    // The selected first item is rendered before the second, so inspect it
    // by rendering a one-item menu as well.
    menu_t one;
    menu_init(&one, s_root_items, 1);
    one.anim_offset_px = 0;
    menu_render(&one);
    MT_CHECK(strcmp(g_last_label, ">Kızılötesi") == 0);
    s_root_items[0].label = "Category";
}

int main(void)
{
    printf("test_menu:\n");
    MT_RUN(banner_has_own_row_and_first_item_remains_selectable);
    MT_RUN(accent_is_local_to_one_menu);
    MT_RUN(hacking_menu_uses_red_text_without_filled_row);
    MT_RUN(press_on_category_enters_submenu);
    MT_RUN(left_returns_to_parent);
    MT_RUN(back_button_returns_to_parent);
    MT_RUN(back_at_top_level_stays_put);
    MT_RUN(back_does_not_change_selection_of_left_menu);
    MT_RUN(press_on_leaf_runs_action);
    MT_RUN(turkish_menu_label_keeps_utf8_characters);
    return MT_SUMMARY();
}
