#pragma once

#include <stdbool.h>

#include "ir_nec.h"

// Brings up RMT RX (on the VS1838B input pin) and RMT TX (on the IR LED
// output pin). Call once at startup, after display_init()/buttons_init().
void ir_driver_init(void);

// Non-blocking: returns true and fills `out_frame` if a complete NEC frame
// was received since the last call. Call this periodically from the main
// loop, same as buttons_poll().
bool ir_driver_poll_rx(ir_nec_frame_t *out_frame);

// Blocking: transmits one NEC frame via the IR LED. Returns once the RMT
// TX has finished sending (typically <70ms for one NEC frame).
void ir_driver_send(const ir_nec_frame_t *frame);
