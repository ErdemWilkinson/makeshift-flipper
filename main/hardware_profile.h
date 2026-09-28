#pragma once

// Joystick UP is on GPIO6. Do NOT move it to GPIO12/13: those are the C6's
// native USB D-/D+ lines. Configuring GPIO13 as a button input disabled the
// native USB port and the pin produced phantom UP presses (4-5 per press).
// RC522 MISO also wants GPIO6, so with UP here RC522 must use another MISO
// pin before BOARD_HAS_RC522 can be enabled.
#define BOARD_UP_GPIO 6
#define BOARD_HAS_RC522 0

#if BOARD_HAS_RC522 && BOARD_UP_GPIO == 6
#error "GPIO6 cannot serve both joystick UP and RC522 SPI MISO"
#endif
