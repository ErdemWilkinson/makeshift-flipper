#pragma once

#include <stdbool.h>
#include <stdint.h>

// Driver for the MFRC522 (RC522 breakout), 13.56MHz ISO14443A over SPI.
// Covers what's needed to detect a card and read its UID — enough for a
// "scan and show UID" screen. Sector read/write (Mifare Classic auth) is
// intentionally not included yet; add it as its own pass once UID reading
// is confirmed working on real hardware.

#define RC522_MAX_UID_LEN 10 // Mifare UIDs are 4, 7, or 10 bytes

typedef struct {
    uint8_t bytes[RC522_MAX_UID_LEN];
    uint8_t length;
} rc522_uid_t;

// Brings up the SPI bus and resets the RC522, with its antenna left OFF
// (see rc522_antenna_on()). Call once at startup.
void rc522_init(void);

// Turns the antenna's RF field on/off. The antenna draws continuous power
// while on, which matters on a LiPo-powered device -- callers should only
// enable it while actively on a scan screen and disable it (or just not
// bother calling rc522_read_uid()) otherwise. rc522_read_uid() does not
// turn the antenna on/off itself; the caller owns that lifecycle.
void rc522_antenna_on(void);
void rc522_antenna_off(void);

typedef enum {
    RC522_SCAN_NO_CARD,      // no PICC in the field -- not an error
    RC522_SCAN_OK,           // out_uid filled with a valid 4-byte UID
    RC522_SCAN_UNSUPPORTED_UID, // a card responded but has a 7/10-byte UID
                                 // (cascade tag 0x88 seen) -- not decoded,
                                 // see the comment in rc522.c for why
    RC522_SCAN_ERROR,        // card responded but the anticollision/BCC
                              // exchange failed (collision, noise, etc.)
} rc522_scan_result_t;

// Non-blocking-ish: does one quick REQA+anticollision pass.
// Safe to call repeatedly from a polling loop (e.g. every 100-200ms) —
// it doesn't block waiting for a card. Requires the antenna to be on
// (see rc522_antenna_on()) or it will simply never see a card.
rc522_scan_result_t rc522_read_uid(rc522_uid_t *out_uid);
