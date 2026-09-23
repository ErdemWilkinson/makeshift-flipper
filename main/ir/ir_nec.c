#include "ir_nec.h"

// NEC timing constants, in microseconds. Real receivers/transmitters drift
// quite a bit, so decoding uses a tolerance window around each of these
// rather than exact matches.
#define LEADER_MARK_US   9000
#define LEADER_SPACE_US  4500
#define BIT_MARK_US      562
#define BIT_SPACE_0_US   562
#define BIT_SPACE_1_US   1687

#define TOLERANCE_PERCENT 25

static bool within_tolerance(uint16_t actual, uint16_t expected)
{
    uint32_t delta = (expected * TOLERANCE_PERCENT) / 100;
    uint32_t lo = expected > delta ? expected - delta : 0;
    uint32_t hi = expected + delta;
    return actual >= lo && actual <= hi;
}

bool ir_nec_decode(const ir_raw_symbol_t *symbols, size_t symbol_count,
                    ir_nec_frame_t *out_frame)
{
    // Leader + 32 bits = 33 symbols. The 33rd symbol's space is the final
    // trailing gap and isn't meaningful, so we don't check it.
    if (symbol_count < 33) {
        return false;
    }
    if (!within_tolerance(symbols[0].mark_us, LEADER_MARK_US) ||
        !within_tolerance(symbols[0].space_us, LEADER_SPACE_US)) {
        return false;
    }

    uint32_t bits = 0;
    for (int i = 0; i < 32; i++) {
        const ir_raw_symbol_t *sym = &symbols[1 + i];

        if (!within_tolerance(sym->mark_us, BIT_MARK_US)) {
            return false;
        }

        bool is_one = within_tolerance(sym->space_us, BIT_SPACE_1_US);
        bool is_zero = within_tolerance(sym->space_us, BIT_SPACE_0_US);
        if (!is_one && !is_zero) {
            return false;
        }

        bits >>= 1;
        if (is_one) {
            bits |= (1UL << 31);
        }
    }

    // NEC transmits LSB-first within each byte, and address arrives before
    // command. The loop above shifts each new bit into bit31 and shifts
    // right, so the first-received bit (address's LSB) ends up in bit0 and
    // the last-received bit (command_inv's MSB) ends up in bit31 -- i.e.
    // byte 0 (address) is the *low* byte of `bits`, not the high one.
    uint8_t address      = bits & 0xFF;
    uint8_t address_inv  = (bits >> 8) & 0xFF;
    uint8_t command       = (bits >> 16) & 0xFF;
    uint8_t command_inv   = (bits >> 24) & 0xFF;

    if ((uint8_t)(address ^ address_inv) != 0xFF) {
        return false;
    }
    if ((uint8_t)(command ^ command_inv) != 0xFF) {
        return false;
    }

    out_frame->address = address;
    out_frame->command = command;
    return true;
}

size_t ir_nec_encode(const ir_nec_frame_t *frame, ir_raw_symbol_t *out_symbols)
{
    size_t n = 0;

    out_symbols[n].mark_us = LEADER_MARK_US;
    out_symbols[n].space_us = LEADER_SPACE_US;
    n++;

    uint8_t bytes[4] = {
        frame->address,
        (uint8_t)~frame->address,
        frame->command,
        (uint8_t)~frame->command,
    };

    for (int b = 0; b < 4; b++) {
        for (int bit = 0; bit < 8; bit++) {
            bool is_one = (bytes[b] >> bit) & 0x1;
            out_symbols[n].mark_us = BIT_MARK_US;
            out_symbols[n].space_us = is_one ? BIT_SPACE_1_US : BIT_SPACE_0_US;
            n++;
        }
    }

    // Trailing mark that closes out the final bit's space.
    out_symbols[n].mark_us = BIT_MARK_US;
    out_symbols[n].space_us = 0;
    n++;

    return n;
}
