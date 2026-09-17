#include "buttons.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "buttons";

// ADC1 channels only live on GPIO0-6 on ESP32-P4. GPIO0/1/2 are taken
// (boot strapping / IR), GPIO7/8 are OLED, so X/Y land on GPIO3/4.
#define JOY_X_ADC_CHANNEL ADC_CHANNEL_3 // GPIO3
#define JOY_Y_ADC_CHANNEL ADC_CHANNEL_4 // GPIO4

#define JOY_PRESS_GPIO GPIO_NUM_22 // center press button
#define BACK_GPIO      GPIO_NUM_23 // standalone back button

#define DEBOUNCE_US 30000

// Raw ADC readings are 0-4095 (12-bit). Rest position is nominally ~2048
// but cheap potentiometer sticks are rarely centered exactly, so this
// reads actual rest values at startup instead of assuming the midpoint.
#define ADC_MAX 4095
#define THRESHOLD_FRACTION_NUM 3 // trigger past 3/8 of the way to an extreme...
#define THRESHOLD_FRACTION_DEN 8
#define RELEASE_FRACTION_NUM   1 // ...and only re-arm once back within 1/8
#define RELEASE_FRACTION_DEN   8

typedef enum {
    AXIS_IDLE,
    AXIS_LOW,   // pushed toward 0
    AXIS_HIGH,  // pushed toward ADC_MAX
} axis_state_t;

static adc_oneshot_unit_handle_t s_adc_handle;
static int s_x_center;
static int s_y_center;
static axis_state_t s_x_state = AXIS_IDLE;
static axis_state_t s_y_state = AXIS_IDLE;

static bool s_press_last_state = true;   // idle = pulled up
static bool s_back_last_state = true;
static int64_t s_press_last_change_us = 0;
static int64_t s_back_last_change_us = 0;

static int read_axis(adc_channel_t channel)
{
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, channel, &raw);
    if (err != ESP_OK) {
        // Rare (ADC busy/timeout); returning 0 would look like "stick
        // slammed to one extreme" and fire a spurious event, so return the
        // channel's own calibrated center instead -- reads as "no motion".
        ESP_LOGW(TAG, "ADC read failed on channel %d: %s", channel, esp_err_to_name(err));
        return (channel == JOY_X_ADC_CHANNEL) ? s_x_center : s_y_center;
    }
    return raw;
}

// Averages a few samples at startup to find each axis's actual rest point,
// since cheap joystick modules are rarely perfectly centered. Warns (but
// still returns a usable value) if the samples disagree too much to trust
// as "resting" -- e.g. the stick was held off-center during boot -- or if
// the result lands too close to either ADC rail, which would make
// axis_update()'s trigger window degenerate.
static int calibrate_center(adc_channel_t channel)
{
    long sum = 0;
    int min_val = ADC_MAX;
    int max_val = 0;
    const int samples = 16;
    for (int i = 0; i < samples; i++) {
        int val = read_axis(channel);
        sum += val;
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
    }
    int center = (int)(sum / samples);

    if (max_val - min_val > ADC_MAX / 16) {
        ESP_LOGW(TAG, "channel %d: noisy calibration (spread %d..%d) -- "
                      "was the stick held off-center at boot?", channel, min_val, max_val);
    }

    // Keep center away from the rails so the trigger/release window in
    // axis_update() can't collapse to near-zero width.
    int rail_margin = ADC_MAX / 8;
    if (center < rail_margin) {
        center = rail_margin;
    } else if (center > ADC_MAX - rail_margin) {
        center = ADC_MAX - rail_margin;
    }
    return center;
}

void buttons_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, JOY_X_ADC_CHANNEL, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, JOY_Y_ADC_CHANNEL, &chan_cfg));

    s_x_center = calibrate_center(JOY_X_ADC_CHANNEL);
    s_y_center = calibrate_center(JOY_Y_ADC_CHANNEL);

    gpio_config_t digital_cfg = {
        .pin_bit_mask = (1ULL << JOY_PRESS_GPIO) | (1ULL << BACK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&digital_cfg);
}

// Turns one axis's raw reading into a state-machine transition. Returns
// the button event to fire (BUTTON_COUNT if none) and updates *state.
static button_id_t axis_update(int raw, int center, axis_state_t *state,
                                button_id_t low_event, button_id_t high_event)
{
    int trigger_low  = center - (center * THRESHOLD_FRACTION_NUM) / THRESHOLD_FRACTION_DEN;
    int trigger_high = center + ((ADC_MAX - center) * THRESHOLD_FRACTION_NUM) / THRESHOLD_FRACTION_DEN;
    int release_low  = center - (center * RELEASE_FRACTION_NUM) / RELEASE_FRACTION_DEN;
    int release_high = center + ((ADC_MAX - center) * RELEASE_FRACTION_NUM) / RELEASE_FRACTION_DEN;

    switch (*state) {
        case AXIS_IDLE:
            if (raw <= trigger_low) {
                *state = AXIS_LOW;
                return low_event;
            }
            if (raw >= trigger_high) {
                *state = AXIS_HIGH;
                return high_event;
            }
            return BUTTON_COUNT;
        case AXIS_LOW:
            if (raw >= release_low) {
                *state = AXIS_IDLE;
            }
            return BUTTON_COUNT;
        case AXIS_HIGH:
            if (raw <= release_high) {
                *state = AXIS_IDLE;
            }
            return BUTTON_COUNT;
    }
    return BUTTON_COUNT;
}

static button_id_t poll_digital(gpio_num_t gpio, bool *last_state,
                                 int64_t *last_change_us, button_id_t event)
{
    int64_t now = esp_timer_get_time();
    bool level = gpio_get_level(gpio) != 0;

    if (level != *last_state) {
        if (now - *last_change_us < DEBOUNCE_US) {
            return BUTTON_COUNT;
        }
        *last_change_us = now;
        *last_state = level;
        if (!level) {
            return event;
        }
    }
    return BUTTON_COUNT;
}

button_id_t buttons_poll(void)
{
    // Digital buttons first: a deliberate press/back-button click should
    // never get preempted by a noisy stick reading.
    button_id_t event = poll_digital(JOY_PRESS_GPIO, &s_press_last_state,
                                      &s_press_last_change_us, BUTTON_PRESS);
    if (event != BUTTON_COUNT) {
        return event;
    }

    event = poll_digital(BACK_GPIO, &s_back_last_state,
                          &s_back_last_change_us, BUTTON_BACK);
    if (event != BUTTON_COUNT) {
        return event;
    }

    // X axis: low raw value = stick pushed left, high = pushed right
    // (wiring-dependent; flip LEFT/RIGHT below if your stick reads inverted).
    int x_raw = read_axis(JOY_X_ADC_CHANNEL);
    event = axis_update(x_raw, s_x_center, &s_x_state, BUTTON_LEFT, BUTTON_RIGHT);
    if (event != BUTTON_COUNT) {
        return event;
    }

    // Y axis: low raw value = stick pushed toward "up" (also wiring-dependent).
    int y_raw = read_axis(JOY_Y_ADC_CHANNEL);
    event = axis_update(y_raw, s_y_center, &s_y_state, BUTTON_UP, BUTTON_DOWN);
    if (event != BUTTON_COUNT) {
        return event;
    }

    return BUTTON_COUNT;
}
