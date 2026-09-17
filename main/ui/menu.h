#pragma once

#include <stddef.h>
#include "input/buttons.h"

// Called when RIGHT or PRESS is used on a menu item.
typedef void (*menu_action_fn)(void);

typedef struct {
    const char *label;
    menu_action_fn on_select; // NULL if this item does nothing yet (placeholder)
} menu_item_t;

typedef struct {
    const menu_item_t *items;
    size_t item_count;
    int selected_index;
    int scroll_offset;

    // Simple slide-in animation state, advanced by menu_animate_tick().
    int anim_offset_px;   // current vertical pixel offset of the whole list (0 = settled)
} menu_t;

void menu_init(menu_t *menu, const menu_item_t *items, size_t item_count);

// Feeds one button event into the menu (moves selection or fires on_select).
// UP/DOWN move the cursor. RIGHT and PRESS both activate the selected item
// (Flipper-style: RIGHT = "enter", PRESS = "confirm" — same action for a flat menu).
// LEFT is reserved for entering a parent list once submenus exist; currently
// a no-op here. Exiting a screen entirely is BUTTON_BACK's job, handled by
// the caller (main.c), not this function.
void menu_handle_button(menu_t *menu, button_id_t button);

// Advances the entry animation by one frame. Call this every loop tick
// before menu_render(). No-op once the animation has settled.
void menu_animate_tick(menu_t *menu);

// Redraws the menu to the display's frame buffer and flushes it.
void menu_render(const menu_t *menu);
