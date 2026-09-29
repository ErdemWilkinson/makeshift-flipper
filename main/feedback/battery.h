#pragma once

#include <stdbool.h>

// Single-cell LiPo fuel gauge by ADC voltage. The battery + terminal is
// expected to reach an ADC-capable pin through a 2:1 resistor divider
// (BATTERY_DIVIDER_NUM/DEN below), because a full cell (4.2 V) exceeds the
// C6 ADC's safe input range -- the divider halves it into range.
//
// Until that divider is soldered, battery_read() returns a "not present"
// reading (valid == false) and the UI shows "--" rather than a fake level.

typedef struct {
    bool valid;        // false when no plausible voltage is present (divider unwired)
    int millivolts;    // reconstructed pack voltage (post-divider math), 0 when !valid
    int percent;       // 0..100 estimate from a LiPo discharge curve, 0 when !valid
} battery_reading_t;

// Sets up the ADC oneshot unit + calibration for the battery pin. Safe to
// call once at startup; leaves the pin high-Z otherwise.
void battery_init(void);

// Takes an averaged reading. Cheap enough to call from the status-bar redraw.
battery_reading_t battery_read(void);
