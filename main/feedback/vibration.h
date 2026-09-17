#pragma once

// Driver for the vibration motor (driven through an NPN transistor on
// GPIO20, per the wiring report — GPIO -> 1k resistor -> base).

// Configures the GPIO as an output, motor off. Call once at startup.
void vibration_init(void);

// Turns the motor on for `duration_ms`, then off. Blocks for that duration
// (fine for short feedback buzzes; don't call with large durations from
// a loop that also needs to stay responsive).
void vibration_pulse(int duration_ms);
