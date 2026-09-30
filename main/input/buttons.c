#include "buttons.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
// has not been verified on this carrier. UP is BOARD_UP_GPIO (GPIO6) in
// hardware_profile.h; never GPIO12/13, the native USB D-/D+ pins.
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
// LEFT: short tap moves the cursor left, long hold exits the keyboard. The old
// 650 ms threshold was shorter than a normal press, so ordinary LEFT taps were
// read as "exit" and the cursor never moved left. Raise it so a deliberate long
// hold is needed to leave, and a normal tap reliably moves left.
#define KEYBOARD_LEFT_HOLD_US 1500000

static bool s_keyboard_right_pending;
static int64_t s_keyboard_right_started_us;
static bool s_keyboard_left_pending;
static int64_t s_keyboard_left_started_us;
// Set once a hold has already fired PRESS/BACK, so the still-held key isn't
// re-armed as a fresh press-then-release (which used to read as a spurious
// short RIGHT/LEFT the instant the user finally released it after a hold).
static bool s_keyboard_right_consumed;
static bool s_keyboard_left_consumed;

typedef struct {
    gpio_num_t pin;
    button_id_t event;
    bool last_state; // true = released
    int64_t last_change_us;
} digital_button_t;

// RIGHT precedes UP/DOWN so an intentional "enter" wins if two inputs change
// in the same polling interval.
//
// GPIO_PRESS (GPIO3) is intentionally NOT in this list: its wire is unverified
// on this carrier, and with nothing driving it the pin floats enough to emit
// spurious PRESS events even with the internal pull-up -- this showed up as the
// radar highlight cycling on its own. The A button already provides confirm, so
// the centre press is not needed. Re-add this row once the GPIO3 wire is
// confirmed and the phantom-press behavior is gone.
static digital_button_t s_buttons[] = {
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

void buttons_wait_all_released(void)
{
    // Wait until every wired button reads released (active-low: high), then
    // reset each button's edge state to "released" so the next buttons_poll()
    // starts clean. This prevents a press made on one screen (e.g. RIGHT to
    // dismiss the legal notice) from leaking into the next screen as a fresh
    // event -- which was making the device jump straight into the RFID submenu
    // at boot. Bounded by a timeout so a permanently-low/floating pin (e.g. an
    // unconnected PRESS line -- see SESSION notes on IO3) can't hang boot.
    int64_t deadline = esp_timer_get_time() + 2000000; // 2 s cap
    for (;;) {
        bool all_released = true;
        for (size_t i = 0; i < BUTTON_COUNT_WIRED; i++) {
            if (gpio_get_level(s_buttons[i].pin) == 0) {
                all_released = false;
                break;
            }
        }
        if (all_released || esp_timer_get_time() >= deadline) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    int64_t now = esp_timer_get_time();
    for (size_t i = 0; i < BUTTON_COUNT_WIRED; i++) {
        s_buttons[i].last_state = true;       // released
        s_buttons[i].last_change_us = now;
    }
    s_keyboard_right_pending = false;
    s_keyboard_left_pending = false;
}

void buttons_keyboard_reset(void)
{
    s_keyboard_right_pending = false;
    s_keyboard_left_pending = false;
    s_keyboard_right_consumed = false;
    s_keyboard_left_consumed = false;
}

// --- Standalone LEFT short/long tracker (for the radar screens) ---
// Screens that use buttons_poll() only get a single BUTTON_BACK from the LEFT
// stick, with no way to tell a tap from a hold. This times the raw LEFT pin so
// a caller can treat a quick tap as "go back one" and a deliberate hold as
// "exit", the same tap-vs-hold feel the keyboard has. Fully self-contained: it
// does not disturb buttons_poll()'s own edge state.
#define RADAR_LEFT_HOLD_US 550000  // >= this held = long (exit)
static bool    s_radar_left_pending;
static int64_t s_radar_left_started_us;
static bool    s_radar_left_consumed;

void buttons_radar_left_reset(void)
{
    s_radar_left_pending = false;
    s_radar_left_consumed = false;
}

int buttons_radar_left_event(void)
{
    int64_t now = esp_timer_get_time();
    bool down = (gpio_get_level(GPIO_LEFT) == 0);

    if (!down) {
        s_radar_left_consumed = false; // released: re-arm for the next press
    }
    if (down && !s_radar_left_pending && !s_radar_left_consumed) {
        s_radar_left_pending = true;
        s_radar_left_started_us = now;
    } else if (down && s_radar_left_pending) {
        if (now - s_radar_left_started_us >= RADAR_LEFT_HOLD_US) {
            s_radar_left_pending = false;
            s_radar_left_consumed = true; // ignore until released
            return 2;                     // long LEFT = exit
        }
    } else if (!down && s_radar_left_pending) {
        s_radar_left_pending = false;
        return 1;                         // released before hold = tap (prev)
    }
    return 0;
}

// Keyboard input, rewritten to track raw pin levels directly instead of
// layering hold-detection on top of buttons_poll()'s edge/debounce events,
// which proved fragile (LEFT taps were never reported, only the long-hold exit
// fired). UP/DOWN still come from the normal debounced poll. RIGHT and LEFT are
// timed here from their own raw GPIO transitions:
//   - RIGHT: tap -> move cursor right; hold >= RIGHT_HOLD -> confirm (PRESS).
//   - LEFT:  tap -> move cursor left;  hold >= LEFT_HOLD  -> exit (BACK).
// Center/A (PRESS) confirms immediately.
button_id_t buttons_poll_keyboard(void)
{
    int64_t now = esp_timer_get_time();

    // Center/A: immediate confirm. Read via the normal poll so it debounces.
    button_id_t event = buttons_poll();
    if (event == BUTTON_PRESS) {
        s_keyboard_right_pending = false;
        s_keyboard_left_pending = false;
        return event;
    }

    // Raw, active-low levels: 0 = pressed, non-zero = released.
    bool right_down = (gpio_get_level(GPIO_RIGHT) == 0);
    bool left_down  = (gpio_get_level(GPIO_LEFT) == 0);

    // --- RIGHT press/hold ---
    if (!right_down) {
        s_keyboard_right_consumed = false; // released: arm for the next press
    }
    if (right_down && !s_keyboard_right_pending && !s_keyboard_right_consumed) {
        s_keyboard_right_pending = true;
        s_keyboard_right_started_us = now;
    } else if (right_down && s_keyboard_right_pending) {
        if (now - s_keyboard_right_started_us >= KEYBOARD_RIGHT_HOLD_US) {
            s_keyboard_right_pending = false;  // consumed as a hold
            s_keyboard_right_consumed = true;  // ignore until the key is released
            return BUTTON_PRESS;               // long RIGHT = confirm
        }
    } else if (!right_down && s_keyboard_right_pending) {
        s_keyboard_right_pending = false;
        return BUTTON_RIGHT;                  // released before hold = move right
    }

    // --- LEFT press/hold ---
    if (!left_down) {
        s_keyboard_left_consumed = false; // released: arm for the next press
    }
    if (left_down && !s_keyboard_left_pending && !s_keyboard_left_consumed) {
        s_keyboard_left_pending = true;
        s_keyboard_left_started_us = now;
    } else if (left_down && s_keyboard_left_pending) {
        if (now - s_keyboard_left_started_us >= KEYBOARD_LEFT_HOLD_US) {
            s_keyboard_left_pending = false;  // consumed as a hold
            s_keyboard_left_consumed = true;  // ignore until the key is released
            return BUTTON_BACK;               // long LEFT = exit
        }
    } else if (!left_down && s_keyboard_left_pending) {
        s_keyboard_left_pending = false;
        return BUTTON_LEFT;                   // released before hold = move left
    }

    // UP/DOWN pass straight through from the debounced poll.
    if (event == BUTTON_UP || event == BUTTON_DOWN) {
        return event;
    }
    return BUTTON_COUNT;
}
