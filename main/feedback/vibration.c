#include "vibration.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

// Matches the wiring report: GPIO20 -> 1k resistor -> transistor base.
#define VIBRATION_GPIO GPIO_NUM_20

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
