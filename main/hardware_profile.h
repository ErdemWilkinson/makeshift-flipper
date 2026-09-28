#pragma once

// Current assembled device: LCD and its controls only. The joystick UP lead
// is on C6 GPIO13 (moved off GPIO6, which RC522 needs for SPI MISO).
// GPIO16/17 do NOT exist as header pins on the actual board (Waveshare
// ESP32-C6-DEV-KIT-NX) -- confirmed from the board photo, its pinout only
// breaks out IO0-IO13 and IO15/IO18-IO23 plus TXD/RXD -- so an earlier
// GPIO16 assignment here was wrong and never wirable. GPIO13 is unclaimed
// elsewhere in this firmware and is not a boot-strapping pin, so it's a safe
// home for UP now that GPIO6 is reserved for RC522. Enabling RC522 still
// requires physically rewiring UP's lead to GPIO13 and MISO to GPIO6, then
// flipping BOARD_HAS_RC522 to 1 -- this #define alone does not move a wire.
#define BOARD_UP_GPIO 13
#define BOARD_HAS_RC522 0

#if BOARD_HAS_RC522 && BOARD_UP_GPIO == 6
#error "GPIO6 cannot serve both joystick UP and RC522 SPI MISO"
#endif
