#pragma once

#include <stdbool.h>

// Waveshare Pico-LCD-1.3's built-in 5-way DIGITAL joystick (5 separate
// GPIOs: up/down/left/right/press, no ADC involved) plus its "B" user
// button, mapped to BUTTON_BACK. This replaces the P4 build's 2-axis
// analog joystick + standalone digital back button -- see buttons.c for
// the GPIO assignments (input/buttons.c's per-pin #defines) and
// KNOWN_ISSUES.md/README.md's pin plan for the module's full pinout.
// The module's A/X/Y buttons are physically present but intentionally
// left unpolled here, reserved for future shortcuts -- this enum and
// buttons_poll()'s contract are unchanged from the analog-joystick build
// so menu.c/text_entry.c/main.c need no changes.
typedef enum {
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,   // stick pushed left: navigation only (not "back")
    BUTTON_RIGHT,  // stick pushed right: navigation / enter
    BUTTON_PRESS,  // center press: confirm / activate
    BUTTON_BACK,   // "B" button: always means "back"
    BUTTON_COUNT,
} button_id_t;

// Configures all 6 polled GPIOs (5-way joystick + B) as digital inputs
// with internal pull-ups. Call once at startup.
void buttons_init(void);

// Samples all 6 polled GPIOs and returns the first newly-activated event,
// if any -- debounced (time-window falling-edge detection, same pattern
// as the analog-joystick build's digital-button debounce). Safe to call
// repeatedly from a single polling loop. Returns BUTTON_COUNT as a "no
// event" sentinel.
button_id_t buttons_poll(void);
