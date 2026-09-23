#pragma once

#include <stdbool.h>
#include <stdint.h>

// Driver for the MFRC522 (RC522 breakout), 13.56MHz ISO14443A over SPI.
// Covers card detection/UID read plus Mifare Classic 1K sector
// authenticate/read/write (this file's second half) for cloning/dumping.
// CRYPTO1 itself is handled entirely by the MFRC522 hardware once
// rc522_authenticate() succeeds -- nothing here implements the cipher.

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

// --- Mifare Classic 1K sector read/write (dump/clone) ---------------------
// Only 4-byte-UID cards (RC522_SCAN_OK from rc522_read_uid()) are supported
// here, same limitation as UID reading. A 1K card has 16 sectors x 4 blocks
// x 16 bytes = 64 blocks total, addressed 0..63 below (not sector/offset
// pairs) -- block (sector*4 + 3) is always that sector's trailer (keys +
// access bits), the other three are data blocks.

#define RC522_BLOCK_SIZE 16
#define RC522_SECTOR_COUNT 16
#define RC522_BLOCKS_PER_SECTOR 4
#define RC522_TOTAL_BLOCKS (RC522_SECTOR_COUNT * RC522_BLOCKS_PER_SECTOR)

typedef enum {
    RC522_KEY_A,
    RC522_KEY_B,
} rc522_key_type_t;

typedef struct {
    uint8_t bytes[6];
} rc522_key_t;

// Common/default Mifare Classic keys (factory default, and a handful of
// widely-reused ones from public key dictionaries), tried in order by
// callers doing a full-card dump. Not exhaustive -- a card using a private
// key won't be crackable by this list, and that sector is simply skipped.
extern const rc522_key_t RC522_DEFAULT_KEYS[];
extern const int RC522_DEFAULT_KEY_COUNT;

// Re-selects the card by UID (REQA + anticollision + SELECT) and then runs
// MFAuthent against `block_addr`'s sector with the given key. Must succeed
// before rc522_read_block()/rc522_write_block() on that sector -- the
// MFRC522 only stays authenticated for the currently-selected sector, so
// this needs to be called again (i.e. the card re-selected) whenever the
// sector being accessed changes, even for the same card. Returns false on
// any failure (card left the field, key rejected, SPI fault); doesn't
// distinguish "wrong key" from "card gone" -- callers that need to try
// several keys should just move to the next one on any false.
bool rc522_authenticate(const rc522_uid_t *uid, uint8_t block_addr,
                         rc522_key_type_t key_type, const rc522_key_t *key);

// Reads one 16-byte block. rc522_authenticate() must have succeeded for
// this block's sector first (see above -- no automatic re-auth here).
bool rc522_read_block(uint8_t block_addr, uint8_t out_data[RC522_BLOCK_SIZE]);

// Writes one 16-byte block. Same authentication precondition as
// rc522_read_block(). Writing a trailer block (block_addr % 4 == 3)
// changes that sector's keys/access bits -- getting the access bits wrong
// can permanently lock the sector (or the whole card) out. Callers should
// only ever write a trailer verbatim as read from a source card, never
// hand-construct one, and should get explicit user confirmation first.
bool rc522_write_block(uint8_t block_addr, const uint8_t data[RC522_BLOCK_SIZE]);

// Ends the current CRYPTO1 session (PICC halt + soft state reset) so the
// next rc522_read_uid()/rc522_authenticate() starts clean. Callers should
// call this when done with a card (success or failure) before moving on
// to a different card or sector flow.
void rc522_stop_crypto(void);

// Attempts to write a new block 0 (UID + BCC + SAK + manufacturer bytes)
// via the "gen1a" magic-card backdoor command sequence (0x40 then 0x43),
// which bypasses normal authentication entirely. Only works on gen1a
// "magic" Mifare Classic clones -- a genuine card, or a gen2 ("CUID")
// clone, will simply not respond to the backdoor and this returns false
// with *out_is_magic set to false so the caller can tell "not a magic
// card" apart from "SPI/other failure". Used for UID cloning, since a
// genuine card's block 0 is factory-locked read-only.
bool rc522_gen1a_write_block0(const uint8_t block0_data[RC522_BLOCK_SIZE],
                               bool *out_is_magic);
