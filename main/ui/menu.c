#include "menu.h"

#include <stdbool.h>
#include <string.h>

#include "display.h"

#define VISIBLE_ROWS DISPLAY_ROWS
#define ROW_HEIGHT_PX 8

// Entry animation: the whole list slides in from the right edge and eases
// into place. Small and dependency-free on purpose (no float math needed).
#define ANIM_START_OFFSET_PX 40
#define ANIM_STEP_DIVISOR 3 // higher = slower ease-out

void menu_init(menu_t *menu, const menu_item_t *items, size_t item_count)
{
    menu->items = items;
    menu->item_count = item_count;
    menu->selected_index = 0;
    menu->scroll_offset = 0;
    menu->anim_offset_px = ANIM_START_OFFSET_PX;
}

static void menu_clamp_scroll(menu_t *menu)
{
    if (menu->selected_index < menu->scroll_offset) {
        menu->scroll_offset = menu->selected_index;
    } else if (menu->selected_index >= menu->scroll_offset + VISIBLE_ROWS) {
        menu->scroll_offset = menu->selected_index - VISIBLE_ROWS + 1;
    }
}

void menu_handle_button(menu_t *menu, button_id_t button)
{
    if (menu->item_count == 0) {
        return;
    }

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
            if (item->on_select != NULL) {
                item->on_select();
            }
            break;
        }
        case BUTTON_LEFT:
            // Reserved for entering a parent list once submenus exist
            // (BUTTON_BACK, not this, is what exits a screen). No-op here.
            break;
        default:
            break;
    }

    menu_clamp_scroll(menu);
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

    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int idx = menu->scroll_offset + row;
        if (idx >= (int)menu->item_count) {
            break;
        }

        int y = row * ROW_HEIGHT_PX;
        // Later rows lag slightly behind earlier ones for a subtle cascade,
        // capped so it never overshoots the base offset.
        int row_lag = row * 2;
        int x = menu->anim_offset_px + row_lag;
        if (menu->anim_offset_px == 0) {
            x = 0; // settled: no per-row lag once animation is done
        }

        bool is_selected = (idx == menu->selected_index);
        char label[DISPLAY_COLS + 1];
        int n = 0;
        label[n++] = is_selected ? '>' : ' ';
        for (int i = 0; menu->items[idx].label[i] != '\0' && n < DISPLAY_COLS; i++) {
            label[n++] = menu->items[idx].label[i];
        }
        label[n] = '\0';

        if (is_selected && menu->anim_offset_px == 0) {
            display_fill_rect(0, y, DISPLAY_WIDTH_PX, ROW_HEIGHT_PX);
            display_draw_text_px(x, y, label, true);
        } else {
            display_draw_text_px(x, y, label, false);
        }
    }

    display_flush();
}
