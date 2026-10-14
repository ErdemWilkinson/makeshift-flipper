#pragma once

// Driver for the feedback output on GPIO1: the active buzzer is wired
// straight to the pin; a motor would need GPIO -> 1k resistor -> NPN base
// plus a flyback diode.

// Configures the GPIO as an output, motor off. Call once at startup.
void vibration_init(void);

// Turns the motor on for `duration_ms`, then off. Blocks for that duration
// (fine for short feedback buzzes; don't call with large durations from
// a loop that also needs to stay responsive).
void vibration_pulse(int duration_ms);
