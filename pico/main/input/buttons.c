#include "buttons.h"

#include "hardware/gpio.h"
#include "pico/time.h"

// Waveshare Pico-LCD-1.3 fixed pinout (module HAT, cannot move -- see the
// migration plan's pin budget table / README's pin plan).
#define PIN_UP    2
#define PIN_DOWN  18
#define PIN_LEFT  16
#define PIN_RIGHT 20
#define PIN_PRESS 3
#define PIN_B     17  // "B" user button -> BUTTON_BACK

// Same debounce window as the P4 build's analog-joystick digital buttons
// (press/back), now applied uniformly to all 6 polled GPIOs.
#define DEBOUNCE_US 30000

typedef struct {
    uint pin;
    button_id_t id;
    bool prev_pressed;
    uint64_t last_change_us;
} polled_button_t;

static polled_button_t s_buttons[] = {
    { PIN_UP,    BUTTON_UP,    false, 0 },
    { PIN_DOWN,  BUTTON_DOWN,  false, 0 },
    { PIN_LEFT,  BUTTON_LEFT,  false, 0 },
    { PIN_RIGHT, BUTTON_RIGHT, false, 0 },
    { PIN_PRESS, BUTTON_PRESS, false, 0 },
    { PIN_B,     BUTTON_BACK,  false, 0 },
};

#define NUM_BUTTONS (sizeof(s_buttons) / sizeof(s_buttons[0]))

void buttons_init(void) {
    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        uint pin = s_buttons[i].pin;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);  // active-low, matches the Waveshare module wiring
    }
}

button_id_t buttons_poll(void) {
    uint64_t now = time_us_64();

    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        polled_button_t *b = &s_buttons[i];
        bool pressed = !gpio_get(b->pin);  // active-low: pulled up, shorted to GND when pressed

        if (pressed != b->prev_pressed) {
            if ((now - b->last_change_us) < DEBOUNCE_US) {
                // Bounce within the debounce window: ignore this edge, keep
                // the previous stable state.
                continue;
            }
            b->last_change_us = now;
            b->prev_pressed = pressed;
            if (pressed) {
                // Newly-activated (falling edge on an active-low input):
                // report it, same "one event per poll call" contract as
                // the analog-joystick build.
                return b->id;
            }
        }
    }

    return BUTTON_COUNT;
}
