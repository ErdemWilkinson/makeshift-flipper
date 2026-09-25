#include "buttons.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hardware_profile.h"

// Waveshare Pico-LCD-1.3 on the ESP32-C6-DEV-KIT-NX carrier, wired with all
// controls as DIRECT GPIO inputs (this LCD breaks every button out to its own
// pin -- there is no TCA9554 I2C expander on it, so the old expander path was
// removed). Each switch is active-low with an internal pull-up: released reads
// high, pressed reads low.
//
// Navigation contract (main.c): RIGHT/PRESS/A = enter/confirm, LEFT = back,
// UP/DOWN = move. On the keyboard, short LEFT moves the cursor and long LEFT
// exits. The centre switch is assigned to GPIO3 in firmware, but its wire
// has not been verified on this carrier. GPIO6 is the current UP lead; RC522
// MISO is disabled for this screen-only hardware profile.
// Moved off the strapping/boot-sensitive pins (old DOWN=IO0, PRESS=IO5):
// holding those low at reset could push the C6 into the wrong boot mode and
// leave the panel blank. DOWN moved to GPIO11, freed by disconnecting B; this
// also avoids RC522 RESET on GPIO2. PRESS uses GPIO3.
#define GPIO_UP    ((gpio_num_t)BOARD_UP_GPIO) // LCD UP
#define GPIO_PRESS GPIO_NUM_3  // LCD joystick centre press (was IO5 strap)
#define GPIO_DOWN  GPIO_NUM_11 // LCD DOWN (moved off RC522 RESET at IO2)
#define GPIO_LEFT  GPIO_NUM_23 // LCD LEFT -> BACK outside keyboard
#define GPIO_RIGHT GPIO_NUM_22 // LCD RIGHT -> enter/confirm
#define GPIO_A     GPIO_NUM_10 // LCD A -> PRESS

#define DEBOUNCE_US 30000
#define KEYBOARD_RIGHT_HOLD_US 650000
#define KEYBOARD_LEFT_HOLD_US 650000

static bool s_keyboard_right_pending;
static int64_t s_keyboard_right_started_us;
static bool s_keyboard_left_pending;
static int64_t s_keyboard_left_started_us;

typedef struct {
    gpio_num_t pin;
    button_id_t event;
    bool last_state; // true = released
    int64_t last_change_us;
} digital_button_t;

// RIGHT precedes UP/DOWN so an intentional "enter" wins if two inputs change
// in the same polling interval.
static digital_button_t s_buttons[] = {
    { GPIO_PRESS, BUTTON_PRESS, true, 0 },
    { GPIO_A,     BUTTON_PRESS, true, 0 },
    { GPIO_RIGHT, BUTTON_RIGHT, true, 0 },
    { GPIO_LEFT,  BUTTON_BACK,  true, 0 },
    { GPIO_UP,    BUTTON_UP,    true, 0 },
    { GPIO_DOWN,  BUTTON_DOWN,  true, 0 },
};

#define BUTTON_COUNT_WIRED (sizeof(s_buttons) / sizeof(s_buttons[0]))

void buttons_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_UP) | (1ULL << GPIO_PRESS) |
                        (1ULL << GPIO_A) |
                        (1ULL << GPIO_DOWN) |
                        (1ULL << GPIO_LEFT) | (1ULL << GPIO_RIGHT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_LOGI("btn", "DIAG init: UP=IO%d DOWN=IO%d PRESS=IO%d; B disabled",
             GPIO_UP, GPIO_DOWN, GPIO_PRESS);
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
    // DIAG: raw levels of UP/DOWN, ~2x/sec. 1=released
    // (pull-up), 0=pressed. If a level never drops to 0 on press, that wire
    // isn't reaching the pin.
    static int64_t s_diag_last;
    int64_t dnow = esp_timer_get_time();
    if (dnow - s_diag_last > 500000) {
        s_diag_last = dnow;
        ESP_LOGI("btn", "DIAG lvl UP(IO%d)=%d DOWN(IO%d)=%d",
                 GPIO_UP, gpio_get_level(GPIO_UP),
                 GPIO_DOWN, gpio_get_level(GPIO_DOWN));
    }
    for (size_t i = 0; i < BUTTON_COUNT_WIRED; i++) {
        digital_button_t *button = &s_buttons[i];
        bool released = gpio_get_level(button->pin) != 0;
        button_id_t event = poll_one(button, released);
        if (event != BUTTON_COUNT) {
            ESP_LOGI("btn", "DIAG: GPIO%d -> event %d", button->pin, event);
            return event;
        }
    }
    return BUTTON_COUNT;
}

void buttons_keyboard_reset(void)
{
    s_keyboard_right_pending = false;
    s_keyboard_left_pending = false;
}

button_id_t buttons_poll_keyboard(void)
{
    button_id_t event = buttons_poll();
    if (event == BUTTON_PRESS) {
        // A/centre take effect immediately and cancel held directions.
        s_keyboard_right_pending = false;
        s_keyboard_left_pending = false;
        return event;
    }
    if (event == BUTTON_RIGHT) {
        s_keyboard_left_pending = false;
        s_keyboard_right_pending = true;
        s_keyboard_right_started_us = esp_timer_get_time();
        return BUTTON_COUNT;
    }
    if (event == BUTTON_BACK && gpio_get_level(GPIO_LEFT) == 0) {
        s_keyboard_right_pending = false;
        s_keyboard_left_pending = true;
        s_keyboard_left_started_us = esp_timer_get_time();
        return BUTTON_COUNT;
    }
    if (s_keyboard_right_pending) {
        if (gpio_get_level(GPIO_RIGHT) != 0) {
            s_keyboard_right_pending = false;
            return BUTTON_RIGHT;
        }
        if (esp_timer_get_time() - s_keyboard_right_started_us >= KEYBOARD_RIGHT_HOLD_US) {
            s_keyboard_right_pending = false;
            return BUTTON_PRESS;
        }
    }
    if (s_keyboard_left_pending) {
        if (gpio_get_level(GPIO_LEFT) != 0) {
            s_keyboard_left_pending = false;
            return BUTTON_LEFT;
        }
        if (esp_timer_get_time() - s_keyboard_left_started_us >= KEYBOARD_LEFT_HOLD_US) {
            s_keyboard_left_pending = false;
            return BUTTON_BACK;
        }
    }
    return event;
}
