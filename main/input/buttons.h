#pragma once

#include <stdbool.h>

// Waveshare Pico-LCD-1.3 controls, all wired as direct GPIO inputs on the
// ESP32-C6-DEV-KIT-NX. Joystick RIGHT enters/confirms and LEFT backs out on
// normal screens; A confirms. B is disabled and GPIO11 is used by DOWN. On
// the keyboard, short LEFT moves left and long LEFT exits. GPIO3 emits PRESS if the
// centre switch is physically wired there (verify on the actual carrier).
typedef enum {
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,   // short physical LEFT in keyboard mode
    BUTTON_RIGHT,  // stick pushed right: navigation / enter
    BUTTON_PRESS,  // center press: confirm / activate (GPIO5, if physically wired)
    BUTTON_BACK,   // stick LEFT outside keyboard, or long LEFT in keyboard mode
    BUTTON_COUNT,
} button_id_t;

// Configures the direct GPIO inputs. Call once at startup.
void buttons_init(void);

// Polls all wired inputs and returns the first newly-pressed one (debounced),
// or BUTTON_COUNT if none. Safe to call repeatedly from a single loop.
button_id_t buttons_poll(void);

// Keyboard-only control mode: short RIGHT/LEFT move the cursor; holding RIGHT
// ~650 ms confirms, holding LEFT ~650 ms exits. A and a wired centre PRESS
// confirm immediately.
// Call reset when entering the keyboard; other screens keep using buttons_poll().
void buttons_keyboard_reset(void);
button_id_t buttons_poll_keyboard(void);
