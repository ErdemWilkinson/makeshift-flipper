// Minimal host-test stand-in for FreeRTOS.h. Only defines what the firmware
// source files this test suite #includes actually need at compile time
// (pdMS_TO_TICKS); no real scheduler exists in the host build.
#pragma once

#define pdMS_TO_TICKS(ms) (ms)
