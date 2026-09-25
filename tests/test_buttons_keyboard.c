// Host test of the physical-button polling and keyboard short/long presses.
#include <stdint.h>

#include "minitest.h"

int64_t g_fake_time_us;
int g_fake_gpio_level[24];
uint64_t g_configured_gpio_mask;

#include "../main/input/buttons.c"

MT_TEST(keyboard_left_short_moves_and_long_exits)
{
    for (int i = 0; i < 24; i++) {
        g_fake_gpio_level[i] = 1;
    }
    buttons_init();
    MT_CHECK((g_configured_gpio_mask & (1ULL << 6)) != 0);
    MT_CHECK((g_configured_gpio_mask & (1ULL << 11)) != 0);

    g_fake_time_us = 100000;
    buttons_keyboard_reset();
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_COUNT);

    g_fake_time_us = 200000;
    g_fake_gpio_level[23] = 0;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_COUNT);
    g_fake_time_us = 250000;
    g_fake_gpio_level[23] = 1;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_LEFT);
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_COUNT);

    g_fake_time_us = 300000;
    g_fake_gpio_level[23] = 0;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_COUNT);
    g_fake_time_us = 950000;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_BACK);
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_COUNT);
    g_fake_time_us = 1000000;
    g_fake_gpio_level[23] = 1;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_COUNT);
}

MT_TEST(keyboard_right_and_a_still_confirm)
{
    g_fake_time_us = 1100000;
    buttons_keyboard_reset();
    g_fake_gpio_level[22] = 0;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_COUNT);
    g_fake_time_us = 1200000;
    g_fake_gpio_level[22] = 1;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_RIGHT);

    g_fake_time_us = 1300000;
    g_fake_gpio_level[22] = 0;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_COUNT);
    g_fake_time_us = 1950000;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_PRESS);
    g_fake_time_us = 2000000;
    g_fake_gpio_level[22] = 1;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_COUNT);

    g_fake_time_us = 2100000;
    g_fake_gpio_level[10] = 0;
    MT_CHECK_EQ_INT(buttons_poll_keyboard(), BUTTON_PRESS);
}

int main(void)
{
    MT_RUN(keyboard_left_short_moves_and_long_exits);
    MT_RUN(keyboard_right_and_a_still_confirm);
    return MT_SUMMARY();
}
