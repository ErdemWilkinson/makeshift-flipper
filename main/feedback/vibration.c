#include "vibration.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

// GPIO15 -> logic-level driver -> motor. A flyback diode is mandatory across
// the motor; never connect the motor directly to the GPIO. Moved off GPIO3,
// which the joystick centre press (buttons.c's GPIO_PRESS) also claims --
// the two cannot share a pin. GPIO15 is a boot strapping pin on the ESP32-C6,
// but it floats free on this board's wiring (unlike GPIO0/GPIO5, it was never
// pulled by anything else here), so a driver input here is safe as long as
// nothing else is wired to it and it isn't held low externally at reset.
#define VIBRATION_GPIO GPIO_NUM_15

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
