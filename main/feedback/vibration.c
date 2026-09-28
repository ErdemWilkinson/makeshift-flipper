#include "vibration.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

// GPIO1 -> logic-level driver -> motor, with a flyback diode across the
// motor; never drive the motor straight from the GPIO. Kept off strapping
// pins because a driver input usually has a pull-down.
#define VIBRATION_GPIO GPIO_NUM_1

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
