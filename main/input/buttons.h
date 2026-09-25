#pragma once

#include <stdbool.h>

// Waveshare Pico-LCD-1.3 controls, all wired as direct GPIO inputs on the
// ESP32-C6-DEV-KIT-NX. The LCD's joystick RIGHT enters/confirms and LEFT is
// mapped to "back"; UP/DOWN navigate. PRESS is still a valid enter event in
// main.c but no physical pin is wired to it in this layout.
typedef enum {
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,   // reserved: secondary actions (e.g. delete) in some screens
    BUTTON_RIGHT,  // stick pushed right: navigation / enter
    BUTTON_PRESS,  // center press: confirm / activate (not wired in this layout)
    BUTTON_BACK,   // stick pushed left: leave the current screen
    BUTTON_COUNT,
} button_id_t;

// Configures the direct GPIO inputs. Call once at startup.
void buttons_init(void);

// Polls all wired inputs and returns the first newly-pressed one (debounced),
// or BUTTON_COUNT if none. Safe to call repeatedly from a single loop.
button_id_t buttons_poll(void);
