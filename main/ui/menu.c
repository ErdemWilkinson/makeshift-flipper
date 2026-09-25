#include "menu.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "display.h"

#define ROW_HEIGHT_PX 16 // matches the 8x16 font

static int visible_rows(const menu_t *menu)
{
    return menu->banner ? DISPLAY_ROWS - 1 : DISPLAY_ROWS;
}

// Entry animation: the whole list slides in from the right edge and eases
// into place. Small and dependency-free on purpose (no float math needed).
// Scaled up from the old 128px-wide OLED's 40px offset to stay proportional
// on the 240px-wide panel.
#define ANIM_START_OFFSET_PX 75
#define ANIM_STEP_DIVISOR 3 // higher = slower ease-out

void menu_init(menu_t *menu, const menu_item_t *items, size_t item_count)
{
    menu->items = items;
    menu->item_count = item_count;
    menu->selected_index = 0;
    menu->scroll_offset = 0;
    menu->anim_offset_px = ANIM_START_OFFSET_PX;
    menu->parent = NULL;
    menu->banner = NULL;
    menu->accent_color = DISPLAY_COLOR_ACCENT;
    menu->text_color = DISPLAY_COLOR_TEXT;
    menu->selected_text_color = DISPLAY_COLOR_ACCENT_TEXT;
    menu->fill_selection = true;
}

void menu_set_banner(menu_t *menu, const char *text)
{
    menu->banner = text;
}

void menu_set_accent(menu_t *menu, display_color_t color)
{
    menu->accent_color = color;
}

void menu_set_text_style(menu_t *menu, display_color_t text_color,
                         display_color_t selected_text_color, bool fill_selection)
{
    menu->text_color = text_color;
    menu->selected_text_color = selected_text_color;
    menu->fill_selection = fill_selection;
}

void menu_assert_fully_wired(const menu_t *menu)
{
    for (size_t i = 0; i < menu->item_count; i++) {
        const menu_item_t *item = &menu->items[i];
        // Both NULL means neither "leaf item with an action" nor "category
        // item with a submenu" -- menu_handle_button()'s RIGHT/PRESS case
        // falls through and does nothing for it. That's only ever a wiring
        // bug (a missing/misindexed menu_link_submenu() call), never an
        // intended state once app_main() has finished linking everything,
        // so fail loudly here instead of leaving a dead button for a user
        // to find later.
        assert((item->on_select != NULL || item->submenu != NULL) &&
               "menu item has neither on_select nor submenu -- missing menu_link_submenu() call?");
    }
}

void menu_link_submenu(menu_t *parent, menu_item_t *parent_item, menu_t *child)
{
    parent_item->submenu = child;
    parent_item->on_select = NULL;
    child->parent = parent;
}

static void menu_clamp_scroll(menu_t *menu)
{
    if (menu->selected_index < menu->scroll_offset) {
        menu->scroll_offset = menu->selected_index;
    } else if (menu->selected_index >= menu->scroll_offset + visible_rows(menu)) {
        menu->scroll_offset = menu->selected_index - visible_rows(menu) + 1;
    }
}

menu_t *menu_handle_button(menu_t *menu, button_id_t button)
{
    if (menu->item_count == 0) {
        return menu;
    }

    // `menu` (the argument) and `next` (the return value) are DIFFERENT
    // things once a submenu/BUTTON_LEFT case runs: UP/DOWN/RIGHT-selecting-
    // a-leaf-item mutate `menu` in place (it's staying the active menu),
    // but entering a submenu or backing out via LEFT reassigns `next` to a
    // *different* menu_t and leaves `menu` untouched. Every case below
    // must pick exactly one of those two behaviors -- if a future case
    // mutates `menu`'s fields (selected_index/scroll_offset) AND also
    // reassigns `next` to something else, the caller ends up rendering
    // `next` while the stale, now-mutated `menu` (the one just left) keeps
    // state that doesn't belong to it anymore. See KNOWN_ISSUES.md.
    menu_t *next = menu;

    switch (button) {
        case BUTTON_UP:
            if (menu->selected_index > 0) {
                menu->selected_index--;
            }
            break;
        case BUTTON_DOWN:
            if (menu->selected_index < (int)menu->item_count - 1) {
                menu->selected_index++;
            }
            break;
        case BUTTON_RIGHT:
        case BUTTON_PRESS: {
            const menu_item_t *item = &menu->items[menu->selected_index];
            if (item->submenu != NULL) {
                next = item->submenu;
                next->anim_offset_px = ANIM_START_OFFSET_PX; // re-trigger entry animation
            } else if (item->on_select != NULL) {
                item->on_select();
            }
            break;
        }
        case BUTTON_LEFT:
        case BUTTON_BACK:
            if (menu->parent != NULL) {
                next = menu->parent;
            }
            break;
        default:
            break;
    }

    menu_clamp_scroll(next);
    return next;
}

void menu_animate_tick(menu_t *menu)
{
    if (menu->anim_offset_px == 0) {
        return;
    }
    // Ease out: cover a fraction of the remaining distance each tick,
    // then snap the last couple pixels so it actually reaches 0.
    int step = menu->anim_offset_px / ANIM_STEP_DIVISOR;
    if (step < 2) {
        step = 2;
    }
    menu->anim_offset_px -= step;
    if (menu->anim_offset_px < 0) {
        menu->anim_offset_px = 0;
    }
}

void menu_render(const menu_t *menu)
{
    display_clear();

    if (menu->banner) {
        int len = 0;
        for (const unsigned char *p = (const unsigned char *)menu->banner; *p; p++) {
            if ((*p & 0xC0) != 0x80) {
                len++;
            }
        }
        if (len > DISPLAY_COLS) {
            len = DISPLAY_COLS;
        }
        int col = (DISPLAY_COLS - len) / 2;
        if (menu->fill_selection) {
            display_fill_rect(0, 0, DISPLAY_WIDTH_PX, ROW_HEIGHT_PX, menu->accent_color);
        }
        display_draw_text_color(0, col, menu->banner,
                                menu->fill_selection ? DISPLAY_COLOR_ACCENT_TEXT
                                                     : menu->selected_text_color);
    }

    for (int row = 0; row < visible_rows(menu); row++) {
        int idx = menu->scroll_offset + row;
        if (idx >= (int)menu->item_count) {
            break;
        }

        int y = (menu->banner ? ROW_HEIGHT_PX : 0) + row * ROW_HEIGHT_PX;
        // Later rows lag slightly behind earlier ones for a subtle cascade,
        // capped so it never overshoots the base offset.
        int row_lag = row * 2;
        int x = menu->anim_offset_px + row_lag;
        if (menu->anim_offset_px == 0) {
            x = 0; // settled: no per-row lag once animation is done
        }

        bool is_selected = (idx == menu->selected_index);
        // A Turkish character occupies two UTF-8 bytes but one display cell.
        char label[DISPLAY_COLS * 4 + 1];
        int n = 0;
        label[n++] = is_selected ? '>' : ' ';
        // DISPLAY_COLS - 1 usable columns after the cursor char above. If
        // the label doesn't fit, truncate and show "..." instead of
        // silently cutting it off mid-word with no indication anything's
        // missing.
        int usable = DISPLAY_COLS - 1;
        const char *source = menu->items[idx].label;
        int label_len = 0;
        for (const unsigned char *p = (const unsigned char *)source; *p; p++) {
            if ((*p & 0xC0) != 0x80) {
                label_len++;
            }
        }
        int keep = label_len <= usable ? usable : usable - 3;
        int copied = 0;
        for (const unsigned char *p = (const unsigned char *)source; *p && copied < keep; ) {
            // Copy a complete UTF-8 sequence, never half of a glyph.
            int bytes = (*p < 0x80) ? 1 : ((*p & 0xE0) == 0xC0) ? 2
                      : ((*p & 0xF0) == 0xE0) ? 3 : ((*p & 0xF8) == 0xF0) ? 4 : 1;
            for (int i = 1; i < bytes; i++) {
                if (p[i] == '\0' || (p[i] & 0xC0) != 0x80) {
                    bytes = 1;
                    break;
                }
            }
            for (int i = 0; i < bytes; i++) {
                label[n++] = (char)*p++;
            }
            copied++;
        }
        if (label_len > usable) {
            for (int i = 0; i < 3 && n < (int)sizeof(label) - 1; i++) {
                label[n++] = '.';
            }
        }
        label[n] = '\0';

        if (is_selected && menu->anim_offset_px == 0) {
            if (menu->fill_selection) {
                display_fill_rect(0, y, DISPLAY_WIDTH_PX, ROW_HEIGHT_PX, menu->accent_color);
            }
            display_draw_text_px(x, y, label, menu->selected_text_color,
                                 menu->fill_selection ? menu->accent_color : display_get_background());
        } else {
            display_draw_text_px(x, y, label, menu->text_color, display_get_background());
        }
    }

    display_flush();
}
