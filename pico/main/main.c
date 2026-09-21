// Phase 1 bring-up main: display + buttons + menu only (migration plan
// Section 7, Phase 1). Every other subsystem (RFID, IR, C6-link,
// vibration, diag persistence) is intentionally stubbed/absent in this
// phase -- the goal here is solely to validate the menu tree renders and
// all 5 joystick directions + PRESS + BACK (button B) navigate correctly
// on real Waveshare Pico-LCD-1.3 + Pico hardware, per
// HARDWARE_TEST_MATRIX.md's "Power-on and display" / "Joystick and
// buttons" rows. Later phases replace this file's menu tree with the
// full one ported from the P4 build's main.c, wiring real actions back
// in as each subsystem is ported.

#include "pico/stdlib.h"

#include "ui/display.h"
#include "ui/menu.h"
#include "input/buttons.h"

static void placeholder_action(void)
{
    // Intentionally empty in Phase 1 -- real actions are wired in as each
    // subsystem is ported (Phases 2-6).
}

static menu_item_t s_main_menu_items[] = {
    { "RFID / NFC",   NULL,                NULL }, // submenu wired in Phase 4
    { "Infrared",     NULL,                NULL }, // submenu wired in Phase 6
    { "Wi-Fi",        NULL,                NULL }, // submenu wired in Phase 3/5
    { "Bluetooth",    NULL,                NULL }, // submenu wired in Phase 3/5
    { "Diagnostics",  placeholder_action,  NULL }, // wired in Phase 2
    { "About",        placeholder_action,  NULL },
};

static menu_t s_main_menu;

int main(void)
{
    stdio_init_all(); // USB stdio -- UART0 is reserved for the C6 link (see CMakeLists.txt)

    display_init();
    buttons_init();

    menu_init(&s_main_menu, s_main_menu_items,
              sizeof(s_main_menu_items) / sizeof(s_main_menu_items[0]));
    // No menu_link_submenu() calls yet in this phase -- every item above
    // either has a real (placeholder) on_select or is deliberately left
    // NULL/NULL (inert) until its owning phase wires it up. Do not call
    // menu_assert_fully_wired() here; it would abort on the intentionally
    // unwired category placeholders above.

    menu_t *current = &s_main_menu;

    while (true) {
        button_id_t btn = buttons_poll();
        if (btn != BUTTON_COUNT) {
            current = menu_handle_button(current, btn);
        }

        menu_animate_tick(current);
        menu_render(current);

        sleep_ms(10);
    }
}
