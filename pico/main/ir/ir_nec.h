#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// NEC IR protocol: 9ms leader mark + 4.5ms leader space, then 32 bits
// (8-bit address, 8-bit inverted address, 8-bit command, 8-bit inverted
// command), each bit = 562us mark + (562us space for 0, 1687us space for 1),
// terminated by a final 562us mark.
typedef struct {
    uint8_t address;
    uint8_t command;
} ir_nec_frame_t;

// One raw RMT symbol: a mark (IR on) duration followed by a space (IR off)
// duration, both in microseconds. This mirrors rmt_symbol_word_t's layout
// so callers can pass RMT driver output through with a plain cast/copy.
typedef struct {
    uint16_t mark_us;
    uint16_t space_us;
} ir_raw_symbol_t;

// Decodes a raw mark/space symbol sequence (as captured by RMT RX) into an
// NEC frame. Returns true on success. Tolerates the usual receiver jitter
// via percentage-based timing windows; returns false on anything that
// doesn't look like a valid NEC frame (wrong leader, bad bit count, or the
// address/command failing their own inverted-byte check).
bool ir_nec_decode(const ir_raw_symbol_t *symbols, size_t symbol_count,
                    ir_nec_frame_t *out_frame);

// Encodes an NEC frame into a raw mark/space symbol sequence ready to hand
// to RMT TX. `out_symbols` must have room for IR_NEC_MAX_SYMBOLS entries.
// Returns the number of symbols written.
#define IR_NEC_MAX_SYMBOLS 34 // 1 leader + 32 bits + 1 trailing mark
size_t ir_nec_encode(const ir_nec_frame_t *frame, ir_raw_symbol_t *out_symbols);
