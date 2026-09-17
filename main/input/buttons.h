#pragma once

#include <stdbool.h>

// 2-axis analog joystick (X/Y potentiometers + center press button) plus
// one standalone digital "back" button. The analog stick's X/Y axes are
// converted into discrete UP/DOWN/LEFT/RIGHT events by thresholding, same
// interface the rest of the UI code already expects.
typedef enum {
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,   // stick pushed left: navigation only (not "back")
    BUTTON_RIGHT,  // stick pushed right: navigation / enter
    BUTTON_PRESS,  // center press: confirm / activate
    BUTTON_BACK,   // standalone digital button: always means "back"
    BUTTON_COUNT,
} button_id_t;

// Configures the joystick's ADC channels (X/Y) and the two digital inputs
// (center press, back button) with internal pull-ups where applicable.
// Call once at startup.
void buttons_init(void);

// Samples the stick and both buttons, and returns the first newly-activated
// event, if any. Debounced (digital buttons) / thresholded with hysteresis
// (stick axes). Safe to call repeatedly from a single polling loop.
// Returns BUTTON_COUNT as a "no event" sentinel.
button_id_t buttons_poll(void);
