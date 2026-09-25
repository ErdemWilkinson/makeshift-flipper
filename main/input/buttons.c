#include "buttons.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_timer.h"

// Waveshare Pico-LCD-1.3 on the ESP32-C6-DEV-KIT-NX carrier, wired with all
// controls as DIRECT GPIO inputs (this LCD breaks every button out to its own
// pin -- there is no TCA9554 I2C expander on it, so the old expander path was
// removed). Each switch is active-low with an internal pull-up: released reads
// high, pressed reads low.
//
// Navigation contract (main.c): RIGHT/PRESS = enter/confirm, BACK = go back,
// UP/DOWN = move. The LCD's LEFT switch is mapped to BUTTON_BACK so that
// pushing the stick left leaves the current screen.
#define GPIO_UP    GPIO_NUM_4  // LCD UP
#define GPIO_DOWN  GPIO_NUM_6  // LCD DOWN
#define GPIO_LEFT  GPIO_NUM_7  // LCD LEFT  -> BACK (leave screen)
#define GPIO_RIGHT GPIO_NUM_22 // LCD RIGHT -> enter/confirm

#define DEBOUNCE_US 30000

typedef struct {
    gpio_num_t pin;
    button_id_t event;
    bool last_state; // true = released
    int64_t last_change_us;
} digital_button_t;

// RIGHT precedes UP/DOWN so an intentional "enter" wins if two inputs change
// in the same polling interval.
static digital_button_t s_buttons[] = {
    { GPIO_RIGHT, BUTTON_RIGHT, true, 0 },
    { GPIO_LEFT,  BUTTON_BACK,  true, 0 },
    { GPIO_UP,    BUTTON_UP,    true, 0 },
    { GPIO_DOWN,  BUTTON_DOWN,  true, 0 },
};

#define BUTTON_COUNT_WIRED (sizeof(s_buttons) / sizeof(s_buttons[0]))

void buttons_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_UP) | (1ULL << GPIO_DOWN) |
                        (1ULL << GPIO_LEFT) | (1ULL << GPIO_RIGHT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

static button_id_t poll_one(digital_button_t *button, bool released)
{
    int64_t now = esp_timer_get_time();
    if (released == button->last_state) {
        return BUTTON_COUNT;
    }
    if (now - button->last_change_us < DEBOUNCE_US) {
        return BUTTON_COUNT;
    }

    button->last_change_us = now;
    button->last_state = released;
    return released ? BUTTON_COUNT : button->event;
}

button_id_t buttons_poll(void)
{
    for (size_t i = 0; i < BUTTON_COUNT_WIRED; i++) {
        digital_button_t *button = &s_buttons[i];
        bool released = gpio_get_level(button->pin) != 0;
        button_id_t event = poll_one(button, released);
        if (event != BUTTON_COUNT) {
            return event;
        }
    }
    return BUTTON_COUNT;
}
