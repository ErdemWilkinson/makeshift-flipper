#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ir_nec.h"

// One flag per receiver. A remote roughly in front of a receiver triggers
// that flag (and often its immediate neighbors too, since VS1838B modules
// have a wide-ish acceptance cone -- this is coarse quadrant sensing, not
// precise bearing).
typedef enum {
    IR_DIR_NORTH = 1 << 0,
    IR_DIR_EAST  = 1 << 1,
    IR_DIR_SOUTH = 1 << 2,
    IR_DIR_WEST  = 1 << 3,
} ir_direction_flag_t;

// Reserved API for a future four-receiver hardware profile. The fixed
// C6-Pico build provides an unavailable stub and does not allocate pins or
// RMT channels for it.
void ir_direction_init(void);

// True once ir_direction_init() has actually run. Callers that offer a
// direction-finding screen should check this first and show an explicit
// "not available" message if false, rather than letting the user sit on a
// screen that will never report anything -- ir_direction_poll() is always
// safe to call either way (it just always returns false when this is
// false), but "safe" and "informative to the user" aren't the same thing.
bool ir_direction_is_available(void);

// Non-blocking: checks all 4 receivers for a decoded NEC frame since the
// last call. Returns true if at least one receiver decoded a valid frame;
// `out_flags` is an OR of ir_direction_flag_t for every receiver that saw
// one (usually just one, sometimes two adjacent ones for a source near a
// boundary between them), and `out_frame` is the frame decoded by
// whichever receiver triggered first this poll (if multiple receivers
// caught the same frame simultaneously, the payload is identical anyway --
// this is one NEC transmission arriving at multiple receivers, not
// separate transmissions). Call this periodically from the main loop, same
// as ir_driver_poll_rx().
bool ir_direction_poll(uint8_t *out_flags, ir_nec_frame_t *out_frame);
