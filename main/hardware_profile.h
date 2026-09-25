#pragma once

// Current assembled device: LCD and its controls only. The joystick UP lead
// is on C6 GPIO6. An RC522 would need GPIO6 for SPI MISO, so it cannot be
// enabled on this wiring without moving one of those leads first.
#define BOARD_UP_GPIO 6
#define BOARD_HAS_RC522 0

#if BOARD_HAS_RC522 && BOARD_UP_GPIO == 6
#error "GPIO6 cannot serve both joystick UP and RC522 SPI MISO"
#endif
