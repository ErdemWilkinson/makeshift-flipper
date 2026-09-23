#pragma once

#include <stdbool.h>

// Waveshare Pico-LCD-1.3 controls on the ESP32-C6-Pico carrier. UP/PRESS
// are direct inputs, DOWN/LEFT/B use the onboard TCA9554 expander, and
// RIGHT is the carrier's shared SDA line. Y is deliberately unsupported.
typedef enum {
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,   // stick pushed left: navigation only (not "back")
    BUTTON_RIGHT,  // stick pushed right: navigation / enter
    BUTTON_PRESS,  // center press: confirm / activate
    BUTTON_BACK,   // standalone digital button: always means "back"
    BUTTON_COUNT,
} button_id_t;

// Configures the direct inputs and the onboard I2C GPIO expander.
// Call once at startup.
void buttons_init(void);

// Polls all six inputs and returns the first newly-pressed one (debounced),
// or BUTTON_COUNT if none. Safe to call repeatedly from a single loop.
button_id_t buttons_poll(void);
