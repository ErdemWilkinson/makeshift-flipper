// Minimal host-test stand-in for freertos/task.h. vTaskDelay is a no-op here
// since host tests drive time via g_fake_time_us, not a real scheduler tick.
#pragma once

static inline void vTaskDelay(int ticks) { (void)ticks; }
