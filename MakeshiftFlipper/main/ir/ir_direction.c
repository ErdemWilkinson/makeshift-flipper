#include "ir_direction.h"

#include <stddef.h>

// The fixed C6-Pico field unit has one IR receiver. The former four-receiver
// direction array has no valid pin/channel allocation in this hardware
// profile, so keep its UI-facing API as an explicit unavailable stub.

void ir_direction_init(void)
{
}

bool ir_direction_is_available(void)
{
    return false;
}

bool ir_direction_poll(uint8_t *out_flags, ir_nec_frame_t *out_frame)
{
    (void)out_frame;
    if (out_flags != NULL) {
        *out_flags = 0;
    }
    return false;
}
