// Host test for main/ui/menu.c's navigation logic (menu_handle_button).

#include <stdbool.h>

#include "minitest.h"
#include "display.h"

void display_clear(void) {}
void display_draw_text(int row, int col, const char *text) { (void)row; (void)col; (void)text; }
void display_draw_text_color(int row, int col, const char *text, display_color_t color) { (void)row; (void)col; (void)text; (void)color; }
void display_draw_text_px(int x, int y, const char *text, display_color_t fg, display_color_t bg) { (void)x; (void)y; (void)text; (void)fg; (void)bg; }
void display_fill_rect(int x, int y, int w, int h, display_color_t color) { (void)x; (void)y; (void)w; (void)h; (void)color; }
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

int main(void)
{
    printf("test_menu:\n");
    MT_RUN(press_on_category_enters_submenu);
    MT_RUN(left_returns_to_parent);
    MT_RUN(back_button_returns_to_parent);
    MT_RUN(back_at_top_level_stays_put);
    MT_RUN(back_does_not_change_selection_of_left_menu);
    MT_RUN(press_on_leaf_runs_action);
    return MT_SUMMARY();
}
