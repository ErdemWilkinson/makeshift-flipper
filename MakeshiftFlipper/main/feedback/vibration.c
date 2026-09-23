#include "vibration.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

// C6-Pico GP26 / GPIO3 -> logic-level driver -> motor. A flyback diode is
// mandatory across the motor; never connect the motor directly to GPIO3.
#define VIBRATION_GPIO GPIO_NUM_3

void vibration_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << VIBRATION_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(VIBRATION_GPIO, 0);
}

void vibration_pulse(int duration_ms)
{
    gpio_set_level(VIBRATION_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level(VIBRATION_GPIO, 0);
}
