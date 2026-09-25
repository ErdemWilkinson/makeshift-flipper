#pragma once

#include <stdint.h>

typedef int gpio_num_t;

#define GPIO_NUM_3 3
#define GPIO_NUM_10 10
#define GPIO_NUM_11 11
#define GPIO_NUM_22 22
#define GPIO_NUM_23 23

#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

extern int g_fake_gpio_level[24];
extern uint64_t g_configured_gpio_mask;

static inline int gpio_config(const gpio_config_t *cfg)
{
    g_configured_gpio_mask = cfg->pin_bit_mask;
    return 0;
}

static inline int gpio_get_level(gpio_num_t pin)
{
    return g_fake_gpio_level[pin];
}

#define ESP_ERROR_CHECK(expr) ((void)(expr))
