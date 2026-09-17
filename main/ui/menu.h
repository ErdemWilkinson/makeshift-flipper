#pragma once

#include <stddef.h>
#include "input/buttons.h"

struct menu_s;

// Called when RIGHT or PRESS is used on a menu item. Mutually exclusive
// with submenu below -- a leaf item has an action, a category item has
// a submenu; a menu_item_t should only set one of the two.
typedef void (*menu_action_fn)(void);

typedef struct {
    const char *label;
    menu_action_fn on_select;   // NULL if this item enters a submenu instead (or is a placeholder)
    struct menu_s *submenu;     // NULL for a leaf item (on_select fires instead)
} menu_item_t;

typedef struct menu_s {
    const menu_item_t *items;
    size_t item_count;
    int selected_index;
    int scroll_offset;

    // Simple slide-in animation state, advanced by menu_animate_tick().
    int anim_offset_px;   // current vertical pixel offset of the whole list (0 = settled)

    // Set by menu_link_submenu() on the child; NULL for a top-level menu.
    // LEFT walks back up this chain.
    struct menu_s *parent;
} menu_t;

// item_count may be 0 for a category placeholder that isn't wired up yet.
void menu_init(menu_t *menu, const menu_item_t *items, size_t item_count);

// Wires parent_item (which must belong to parent->items) to open child when
// selected, and records parent as child's parent so LEFT can back out of
// it later. Must be called after both menus are menu_init()'d.
void menu_link_submenu(menu_t *parent, menu_item_t *parent_item, menu_t *child);

// Feeds one button event into the menu (moves selection, fires on_select,
// or enters/exits a submenu) and returns the menu_t that should be
// rendered next (may be `menu` itself, its submenu, or its parent -- never
// NULL). UP/DOWN move the cursor. RIGHT and PRESS activate the selected
// item: for a leaf item this calls on_select (Flipper-style, both do the
// same thing in a flat menu); for a category item this enters its
// submenu. LEFT backs out to the parent menu if one exists (top-level
// menus have no parent, so LEFT is a no-op there). Exiting a screen
// (leaving the menu system entirely) is BUTTON_BACK's job, handled by the
// caller (main.c), not this function.
menu_t *menu_handle_button(menu_t *menu, button_id_t button);

// Advances the entry animation by one frame. Call this every loop tick
// before menu_render(). No-op once the animation has settled.
void menu_animate_tick(menu_t *menu);

// Redraws the menu to the display's frame buffer and flushes it.
void menu_render(const menu_t *menu);
