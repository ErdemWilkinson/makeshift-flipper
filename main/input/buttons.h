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

// Blocks until all wired buttons are released (or ~2 s elapse), then clears the
// debounce/edge state so the next buttons_poll() can't report a press that was
// really made on a previous screen. Call after any screen that consumed a press
// to leave it, before returning to the menu -- notably after the boot notice,
// so dismissing it doesn't fall through into the first menu item.
void buttons_wait_all_released(void);

// Polls all wired inputs and returns the first newly-pressed one (debounced),
// or BUTTON_COUNT if none. Safe to call repeatedly from a single loop.
button_id_t buttons_poll(void);

// Keyboard-only control mode: short RIGHT/LEFT move the cursor; holding RIGHT
// ~650 ms confirms, holding LEFT ~650 ms exits. A and a wired centre PRESS
// confirm immediately.
// Call reset when entering the keyboard; other screens keep using buttons_poll().
void buttons_keyboard_reset(void);
button_id_t buttons_poll_keyboard(void);

// Standalone LEFT tap-vs-hold tracker for the radar screens, where buttons_poll()
// alone can't distinguish a quick LEFT tap from a hold. Call reset when entering
// the screen, then call the event function every loop:
//   returns 0 = nothing, 1 = LEFT tapped (e.g. move to previous target),
//           2 = LEFT held long enough to mean "exit".
// Self-contained: does not disturb buttons_poll()'s state, so RIGHT/UP/DOWN can
// still be read from buttons_poll() in the same loop.
void buttons_radar_left_reset(void);
int  buttons_radar_left_event(void);
