#include "buttons.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

// Waveshare ESP32-C6-Pico fixed wiring:
// - UP and PRESS are direct C6 inputs.
// - DOWN, LEFT and B/BACK are routed through the onboard TCA9554.
// - RIGHT shares the physical SDA line. It is sampled only while the bus is
//   idle; no TCA transaction is attempted while the switch holds SDA low.
// - Y shares SCL and is deliberately unsupported.
//
// The RIGHT arrangement is imposed by stacking the Pico-LCD-1.3 onto this
// carrier and needs hardware validation. Do not attach any other non-I2C
// load to GPIO22/GPIO23.
#define GPIO_UP       GPIO_NUM_4  // Pico GP2
#define GPIO_PRESS    GPIO_NUM_5  // Pico GP3
#define I2C_SDA_GPIO  GPIO_NUM_22 // Pico GP20 / joystick RIGHT
#define I2C_SCL_GPIO  GPIO_NUM_23 // Pico GP21 / button Y (unused)

#define TCA9554_ADDRESS       0x20
#define TCA9554_REG_INPUT     0x00
#define TCA9554_REG_OUTPUT    0x01
#define TCA9554_REG_CONFIG    0x03
#define TCA9554_DOWN_BIT      3
#define TCA9554_BACK_BIT      4
#define TCA9554_LEFT_BIT      5
#define TCA9554_INPUT_MASK    ((1U << TCA9554_DOWN_BIT) | \
                               (1U << TCA9554_BACK_BIT) | \
                               (1U << TCA9554_LEFT_BIT))

#define DEBOUNCE_US 30000
#define I2C_TIMEOUT_MS 20
#define EXPANDER_RETRY_US 1000000

static const char *TAG = "buttons";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_tca9554;
static bool s_expander_ready;
static bool s_expander_error_logged;
static bool s_bus_reset_pending;
static int64_t s_last_expander_retry_us;

typedef enum {
    BUTTON_SOURCE_DIRECT,
    BUTTON_SOURCE_EXPANDER,
    BUTTON_SOURCE_I2C_SDA,
} button_source_t;

typedef struct {
    button_source_t source;
    uint8_t pin;
    button_id_t event;
    bool last_state; // true = released
    int64_t last_change_us;
} digital_button_t;

// PRESS/BACK precede navigation so an intentional click wins if two inputs
// change in the same polling interval.
static digital_button_t s_buttons[] = {
    { BUTTON_SOURCE_DIRECT,   GPIO_PRESS,          BUTTON_PRESS, true, 0 },
    { BUTTON_SOURCE_EXPANDER, TCA9554_BACK_BIT,    BUTTON_BACK,  true, 0 },
    { BUTTON_SOURCE_DIRECT,   GPIO_UP,             BUTTON_UP,    true, 0 },
    { BUTTON_SOURCE_EXPANDER, TCA9554_DOWN_BIT,    BUTTON_DOWN,  true, 0 },
    { BUTTON_SOURCE_EXPANDER, TCA9554_LEFT_BIT,    BUTTON_LEFT,  true, 0 },
    { BUTTON_SOURCE_I2C_SDA,  I2C_SDA_GPIO,        BUTTON_RIGHT, true, 0 },
};

#define BUTTON_GPIO_COUNT (sizeof(s_buttons) / sizeof(s_buttons[0]))

static esp_err_t tca9554_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_tca9554, &reg, 1, value, 1,
                                       I2C_TIMEOUT_MS);
}

static esp_err_t tca9554_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(s_tca9554, data, sizeof(data), I2C_TIMEOUT_MS);
}

static bool tca9554_init(void)
{
    esp_err_t err = ESP_OK;
    if (s_i2c_bus == NULL) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = I2C_SDA_GPIO,
            .scl_io_num = I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    if (s_tca9554 == NULL) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = TCA9554_ADDRESS,
            .scl_speed_hz = 100000,
        };
        err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_tca9554);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TCA9554 attach failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    uint8_t output;
    uint8_t config;
    err = tca9554_read_reg(TCA9554_REG_OUTPUT, &output);
    if (err == ESP_OK) {
        err = tca9554_read_reg(TCA9554_REG_CONFIG, &config);
    }
    if (err == ESP_OK) {
        // Preserve the other expander pins. A high configuration bit makes
        // a pin an input; a high output latch avoids pulling it low if its
        // mode is changed during a later board-support update.
        err = tca9554_write_reg(TCA9554_REG_OUTPUT,
                                (uint8_t)(output | TCA9554_INPUT_MASK));
    }
    if (err == ESP_OK) {
        err = tca9554_write_reg(TCA9554_REG_CONFIG,
                                (uint8_t)(config | TCA9554_INPUT_MASK));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 configuration failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

void buttons_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_UP) | (1ULL << GPIO_PRESS),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    s_expander_ready = tca9554_init();
    if (!s_expander_ready) {
        ESP_LOGE(TAG, "expander buttons disabled; UP/PRESS/RIGHT remain available");
    }
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
    // At bus idle both lines must be high. The LCD's physical RIGHT switch
    // pulls SDA low, so recognize it before any I2C transaction. Y pulls SCL
    // low and is ignored; in either case skip the expander until release.
    bool scl_released = gpio_get_level(I2C_SCL_GPIO) != 0;
    bool right_released = gpio_get_level(I2C_SDA_GPIO) != 0;
    bool bus_available = scl_released && right_released;

    int64_t now = esp_timer_get_time();
    if (!s_expander_ready && bus_available &&
        now - s_last_expander_retry_us >= EXPANDER_RETRY_US) {
        s_last_expander_retry_us = now;
        s_expander_ready = tca9554_init();
        if (s_expander_ready) {
            s_expander_error_logged = false;
            ESP_LOGI(TAG, "TCA9554 recovered");
        }
    }

    uint8_t expander_inputs = 0xFF;
    bool have_expander_sample = false;
    if (s_expander_ready && bus_available) {
        esp_err_t err = ESP_OK;
        if (s_bus_reset_pending) {
            err = i2c_master_bus_reset(s_i2c_bus);
            if (err == ESP_OK) {
                s_bus_reset_pending = false;
            }
        }
        if (err == ESP_OK) {
            err = tca9554_read_reg(TCA9554_REG_INPUT, &expander_inputs);
        }
        if (err == ESP_OK) {
            have_expander_sample = true;
            s_expander_error_logged = false;
        } else {
            s_bus_reset_pending = true;
            if (!s_expander_error_logged) {
                ESP_LOGW(TAG, "TCA9554 read failed: %s", esp_err_to_name(err));
                s_expander_error_logged = true;
            }
        }
    }

    for (size_t i = 0; i < BUTTON_GPIO_COUNT; i++) {
        digital_button_t *button = &s_buttons[i];
        bool released;

        if (button->source == BUTTON_SOURCE_DIRECT) {
            released = gpio_get_level((gpio_num_t)button->pin) != 0;
        } else if (button->source == BUTTON_SOURCE_I2C_SDA) {
            released = right_released;
        } else {
            if (!have_expander_sample) {
                continue;
            }
            released = (expander_inputs & (1U << button->pin)) != 0;
        }

        button_id_t event = poll_one(button, released);
        if (event != BUTTON_COUNT) {
            return event;
        }
    }

    return BUTTON_COUNT;
}
