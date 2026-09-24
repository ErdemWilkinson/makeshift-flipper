#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "diag/diag.h"
#include "input/buttons.h"
#include "ir/ir_direction.h"
#include "ir/ir_driver.h"
#include "ir/ir_library.h"
#include "net/c6_link.h"
#include "rfid/rc522.h"
#include "rfid/rdm6300.h"
#include "rfid/rfid_library.h"
#include "feedback/vibration.h"
#include "ui/display.h"
#include "ui/menu.h"
#include "ui/text_entry.h"

static const char *TAG = "main";

typedef enum {
    APP_SCREEN_MENU,
    APP_SCREEN_SCAN_125KHZ,
    APP_SCREEN_SCAN_1356MHZ,
    APP_SCREEN_IR_DIRECTION,
} app_screen_t;

static app_screen_t s_screen = APP_SCREEN_MENU;
static bool s_screen_dirty = true; // forces a render on the next loop tick

// The menu the main loop is currently rendering/feeding button events to --
// starts at the top-level menu, moves to a submenu's menu_t and back as
// menu_handle_button() walks the tree. Blocking actions (WiFi setup,
// Errors, ...) re-render *this* (whichever submenu they were launched
// from) on return, not necessarily the top-level menu.
static menu_t *s_active_menu;

// Set when a scan screen just found a tag, so its render function can show
// the result instead of "scanning...". Cleared when BACK returns to the menu.
static char s_last_scan_line[DISPLAY_COLS + 1];

// Tracks whether a tag is believed to still be sitting in the reader's
// field, so the scan screen's main-loop block (below) can fire
// vibration_pulse()/diag_record_error() once per presentation instead of
// once per ~10ms poll for as long as the tag stays there -- see
// KNOWN_ISSUES.md's Round 19 entry ("held-card vibration/diagnostic
// flood"). Reset (along with s_last_scan_line) whenever a scan screen is
// (re-)entered.
static bool s_scan_card_present;

// RC522 reports a real RC522_SCAN_NO_CARD result the moment a 13.56MHz tag
// leaves the field, so that's used directly there. The RDM6300 (125kHz) has
// no such signal -- it's a read-only module that just streams a frame
// periodically for as long as a tag is present, with nothing to say
// "gone" -- so absence there is inferred from a run of consecutive silent
// polls instead. At the ~10ms poll cadence this loop runs at, 15 misses is
// ~150ms of the tag not being re-read, comfortably longer than one
// RDM6300 resend interval but still short enough that pulling the tag away
// re-arms detection almost immediately.
#define RDM6300_ABSENCE_POLLS 15
static int s_rdm6300_miss_streak;

// Edge-latch for the 13.56MHz RC522_SCAN_ERROR path. Without it, a broken/
// unplugged reader returns RC522_SCAN_ERROR on every ~10ms poll and would
// overwrite all DIAG_HISTORY_CAPACITY slots in a fraction of a second,
// erasing the earlier failures the Errors menu is meant to preserve. Record
// the transition into the error state once; only re-arm after a
// non-error poll (OK / no-card / unsupported) clears it.
static bool s_rc522_scan_error_latched;

static void action_rfid_125khz(void)
{
    s_screen = APP_SCREEN_SCAN_125KHZ;
    s_last_scan_line[0] = '\0';
    s_scan_card_present = false;
    s_rdm6300_miss_streak = 0;
    s_screen_dirty = true;
}

static void action_nfc_1356mhz(void)
{
    s_screen = APP_SCREEN_SCAN_1356MHZ;
    s_last_scan_line[0] = '\0';
    s_scan_card_present = false;
    s_rc522_scan_error_latched = false;
    s_screen_dirty = true;
    rc522_antenna_on(); // draws continuous power; only while this screen is active
}

// --- Mifare Classic dump/clone -------------------------------------------
// One sector's worth of blocks plus whether it was actually readable (a
// sector using a key outside RC522_DEFAULT_KEYS is left zeroed and marked
// unreadable rather than aborting the whole dump).
typedef struct {
    uint8_t blocks[RC522_BLOCKS_PER_SECTOR][RC522_BLOCK_SIZE];
    bool readable;
    rc522_key_type_t key_type; // which key type unlocked it (valid if readable)
    rc522_key_t key;           // the key itself (valid if readable) -- needed
                                // again at write time, since auth doesn't
                                // carry over from the read pass to a
                                // different (target) card
} rc522_sector_dump_t;

typedef struct {
    rc522_uid_t uid;
    rc522_sector_dump_t sectors[RC522_SECTOR_COUNT];
    int sectors_read;
} rc522_card_dump_t;

// Blocks until any button is pressed. Used after a result screen (success/
// error message, sector dump, scan results, ... already drawn and flushed
// by the caller) so the user has time to read it before the screen
// changes. Shared by most of the "show a result, then wait" action
// functions below -- see individual callers for what precedes it.
static void wait_for_any_key(void)
{
    button_id_t any;
    do {
        any = buttons_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (any == BUTTON_COUNT);
}

// Waits (blocking, antenna must already be on) for any card and returns its
// UID. Used by both the "scan source"/"place target card" steps of
// dump/clone and by action_rfid_save_1356mhz() -- polls at the same ~10ms
// cadence as the main loop's own scan screen. BACK cancels and returns
// false.
//
// A 7/10-byte UID (RC522_SCAN_UNSUPPORTED_UID -- rc522.c only implements
// cascade level 1) used to fall through the `result == RC522_SCAN_OK` check
// silently and keep polling forever: every caller here needs a 4-byte UID
// (clone/dump only handles those; Save 13.56MHz's header comment claiming
// 7/10-byte support was aspirational, not backed by rc522_read_uid()), so
// there was no way to make progress with such a card in range other than
// BACK -- no error, no explanation, just an unresponsive-looking "Present
// tag now" screen. Now reports it explicitly and returns false like a
// cancel, so callers show a real result instead of a silent hang. See
// KNOWN_ISSUES.md's Round 19 entry.
static bool wait_for_card(const char *title, const char *prompt_line, const char *diag_module,
                          rc522_uid_t *out_uid)
{
    for (;;) {
        display_clear();
        display_draw_text_color(0, 0, title, DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, prompt_line);
        display_draw_text(6, 0, "BACK: cancel");
        display_flush();

        for (int i = 0; i < 20; i++) { // ~200ms between redraws
            button_id_t event = buttons_poll();
            if (event == BUTTON_BACK) {
                return false;
            }
            rc522_scan_result_t result = rc522_read_uid(out_uid);
            if (result == RC522_SCAN_OK) {
                return true;
            }
            if (result == RC522_SCAN_UNSUPPORTED_UID) {
                display_clear();
                display_draw_text_color(0, 0, title, DISPLAY_COLOR_ACCENT);
                display_draw_text_color(2, 0, "7/10-byte UID", DISPLAY_COLOR_ERROR);
                display_draw_text_color(3, 0, "not supported", DISPLAY_COLOR_ERROR);
                display_draw_text(6, 0, "Press any key");
                display_flush();
                diag_record_error(diag_module, "RC522_SCAN_UNSUPPORTED_UID");
                wait_for_any_key();
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// Boot splash: DEVICE_NAME on an accent-filled bar, held for a couple
// seconds before the main menu takes over. Purely cosmetic (no button
// short-circuits it -- it's deliberately brief enough that waiting it out
// isn't annoying) but it's the first thing a user sees, so it's where the
// device's own identity (name + color theme, see display.h) actually
// registers, rather than jumping straight to a generic menu list.
static void render_boot_splash(void)
{
    display_clear();

    // Accent bar roughly centered vertically, DEVICE_NAME centered inside
    // it. DISPLAY_ROWS/COLS are 15/30 -- these row/col numbers are tuned
    // for that grid, not derived from strlen(), since the bar's height is
    // also a deliberate visual choice, not just "however tall the text is".
    const int bar_row0 = 5;
    const int bar_row1 = 9;
    display_fill_rect(0, bar_row0 * 16, DISPLAY_WIDTH_PX, (bar_row1 - bar_row0) * 16,
                       DISPLAY_COLOR_ACCENT);

    int name_len = (int)strlen(DEVICE_NAME);
    int name_col = (DISPLAY_COLS - name_len) / 2;
    if (name_col < 0) {
        name_col = 0;
    }
    display_draw_text_color(7, name_col, DEVICE_NAME, DISPLAY_COLOR_ACCENT_TEXT);

    static const char *tagline = "RFID - IR - WiFi - BT";
    int tagline_len = (int)strlen(tagline);
    int tagline_col = (DISPLAY_COLS - tagline_len) / 2;
    if (tagline_col < 0) {
        tagline_col = 0;
    }
    display_draw_text_color(11, tagline_col, tagline, DISPLAY_COLOR_DIM);

    display_flush();
    vTaskDelay(pdMS_TO_TICKS(1500));
}

// Authenticates and reads all 16 sectors of `dump->uid`'s card, trying each
// of RC522_DEFAULT_KEYS as Key A then Key B per sector. A sector whose key
// isn't in that list is left with sectors[i].readable = false rather than
// aborting the rest of the dump -- see rc522_sector_dump_t.
static void dump_card(rc522_card_dump_t *dump)
{
    dump->sectors_read = 0;
    for (int sector = 0; sector < RC522_SECTOR_COUNT; sector++) {
        rc522_sector_dump_t *sd = &dump->sectors[sector];
        sd->readable = false;
        uint8_t trailer_block = (uint8_t)(sector * RC522_BLOCKS_PER_SECTOR + 3);

        bool authed = false;
        for (int k = 0; k < RC522_DEFAULT_KEY_COUNT && !authed; k++) {
            if (rc522_authenticate(&dump->uid, trailer_block, RC522_KEY_A,
                                    &RC522_DEFAULT_KEYS[k])) {
                sd->key_type = RC522_KEY_A;
                sd->key = RC522_DEFAULT_KEYS[k];
                authed = true;
            }
        }
        for (int k = 0; k < RC522_DEFAULT_KEY_COUNT && !authed; k++) {
            if (rc522_authenticate(&dump->uid, trailer_block, RC522_KEY_B,
                                    &RC522_DEFAULT_KEYS[k])) {
                sd->key_type = RC522_KEY_B;
                sd->key = RC522_DEFAULT_KEYS[k];
                authed = true;
            }
        }
        if (!authed) {
            memset(sd->blocks, 0, sizeof(sd->blocks));
            continue;
        }

        bool sector_ok = true;
        for (int b = 0; b < RC522_BLOCKS_PER_SECTOR; b++) {
            uint8_t block_addr = (uint8_t)(sector * RC522_BLOCKS_PER_SECTOR + b);
            if (!rc522_read_block(block_addr, sd->blocks[b])) {
                sector_ok = false;
                break;
            }
        }
        sd->readable = sector_ok;
        if (sector_ok) {
            dump->sectors_read++;
        }
    }
    rc522_stop_crypto();
}

// Shows the dump one sector at a time (UP/DOWN moves between sectors,
// PRESS/RIGHT continues past the summary to the clone step, BACK cancels
// out entirely). Returns true if the user chose to continue.
// Row 0 is the header, rows 1..(RC522_BLOCKS_PER_SECTOR*2) are the 2-rows-
// per-block dump body, and the footer needs 2 more rows below that --
// catch it at compile time if a future RC522_BLOCKS_PER_SECTOR change (or
// a smaller DISPLAY_ROWS) would make the dump body overlap the footer.
_Static_assert(1 + RC522_BLOCKS_PER_SECTOR * 2 + 2 <= DISPLAY_ROWS,
               "RC522 sector dump body + footer taller than the display");

static bool show_dump_and_confirm(const rc522_card_dump_t *dump)
{
    int sector = 0;
    for (;;) {
        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "Sector %d/%d %s", sector, RC522_SECTOR_COUNT - 1,
                  dump->sectors[sector].readable ? "" : "(locked)");
        display_draw_text_color(0, 0, header, DISPLAY_COLOR_ACCENT);

        if (dump->sectors[sector].readable) {
            // RC522_BLOCK_SIZE is 16 bytes (32 hex chars), too wide for one
            // DISPLAY_COLS-wide row even on the larger LCD -- split each
            // block across 2 rows of 8 bytes, so all 16 bytes of every
            // block are shown (the old 21-column OLED layout only ever
            // showed the first 8 of each block's 16 bytes).
            for (int b = 0; b < RC522_BLOCKS_PER_SECTOR; b++) {
                const uint8_t *block = dump->sectors[sector].blocks[b];
                for (int half = 0; half < 2; half++) {
                    char line[DISPLAY_COLS + 1];
                    int n = 0;
                    for (int i = 0; i < 8 && n < DISPLAY_COLS - 2; i++) {
                        n += snprintf(&line[n], sizeof(line) - n, "%02X", block[half * 8 + i]);
                    }
                    display_draw_text(1 + b * 2 + half, 0, line);
                }
            }
        } else {
            display_draw_text(2, 0, "No default key worked");
        }

        char footer[DISPLAY_COLS + 1];
        snprintf(footer, sizeof(footer), "%d/%d sectors read", dump->sectors_read, RC522_SECTOR_COUNT);
        display_draw_text(DISPLAY_ROWS - 2, 0, footer);
        display_draw_text(DISPLAY_ROWS - 1, 0, "PRESS:clone BACK:exit");
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK) {
            return false;
        }
        if (event == BUTTON_PRESS || event == BUTTON_RIGHT) {
            return true;
        }
        if (event == BUTTON_UP && sector > 0) {
            sector--;
        } else if (event == BUTTON_DOWN && sector < RC522_SECTOR_COUNT - 1) {
            sector++;
        }
    }
}

// Writes every readable sector from `dump` onto the card at `target_uid`,
// data blocks always, trailer blocks (keys + access bits) only if
// `write_trailers` -- getting a trailer's access bits wrong can lock a
// sector permanently, so this is opt-in and separately confirmed. Returns
// the number of sectors fully written.
static int clone_to_card(const rc522_uid_t *target_uid, const rc522_card_dump_t *dump,
                          bool write_trailers)
{
    int written = 0;
    for (int sector = 0; sector < RC522_SECTOR_COUNT; sector++) {
        const rc522_sector_dump_t *sd = &dump->sectors[sector];
        if (!sd->readable) {
            continue;
        }
        uint8_t trailer_block = (uint8_t)(sector * RC522_BLOCKS_PER_SECTOR + 3);
        if (!rc522_authenticate(target_uid, trailer_block, sd->key_type, &sd->key)) {
            continue; // target card doesn't share this sector's key -- skip it
        }

        bool sector_ok = true;
        int blocks_to_write = write_trailers ? RC522_BLOCKS_PER_SECTOR : RC522_BLOCKS_PER_SECTOR - 1;
        for (int b = 0; b < blocks_to_write; b++) {
            uint8_t block_addr = (uint8_t)(sector * RC522_BLOCKS_PER_SECTOR + b);
            if (block_addr == 0) {
                continue; // block 0 (UID/BCC/SAK) is handled separately via gen1a, never here
            }
            if (!rc522_write_block(block_addr, sd->blocks[b])) {
                sector_ok = false;
                break;
            }
        }
        if (sector_ok) {
            written++;
        }
    }
    rc522_stop_crypto();
    return written;
}

// Full dump -> clone flow: scan a source card, read every sector it'll give
// up a key for, show the result, then (on confirmation) wait for a target
// card and write the same data/trailers onto it. Also offers a gen1a
// UID-clone pass first if the target answers the magic backdoor, since a
// genuine card's block 0 can never be written normally. Fully blocking,
// same pattern as the other network/hardware-backed actions in this file.
static void action_rfid_clone(void)
{
    rc522_antenna_on();

    // Heap-allocated (not stack) specifically because rc522_card_dump_t is
    // ~1.1KB (16 sectors x 4 blocks x 16 bytes + bookkeeping) and this
    // function is called from the main task's own stack, not a dedicated
    // one -- see KNOWN_ISSUES.md. A failure here was previously silent
    // beyond just bailing out to the menu; now it's recorded like every
    // other failure mode in this action, so it shows up in "Errors" for
    // anyone who hits it instead of looking like the button did nothing.
    // calloc: a sector whose block read fails mid-way must not leave
    // indeterminate bytes behind.
    rc522_card_dump_t *dump = calloc(1, sizeof(rc522_card_dump_t));
    if (dump == NULL) {
        diag_record_error("RFID Clone", "RC522_CLONE_OUT_OF_MEMORY");
        rc522_antenna_off();
        menu_render(s_active_menu);
        return;
    }

    if (!wait_for_card("RFID Clone", "Place source card", "RFID Clone", &dump->uid)) {
        free(dump);
        rc522_antenna_off();
        menu_render(s_active_menu);
        return;
    }

    display_clear();
    display_draw_text_color(0, 0, "RFID Clone", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Reading sectors...");
    display_flush();
    dump_card(dump);

    if (dump->sectors_read == 0) {
        diag_record_error("RFID Clone", "RC522_DUMP_NO_SECTORS");
    }

    if (!show_dump_and_confirm(dump)) {
        free(dump);
        rc522_antenna_off();
        menu_render(s_active_menu);
        return;
    }

    rc522_uid_t target_uid;
    if (!wait_for_card("RFID Clone", "Place TARGET card", "RFID Clone", &target_uid)) {
        free(dump);
        rc522_antenna_off();
        menu_render(s_active_menu);
        return;
    }

    // UID clone is a separate, best-effort pass via the gen1a backdoor --
    // most targets won't be magic cards, and that's fine, data cloning
    // below doesn't depend on it.
    // Block 0 is only trustworthy if sector 0 was fully read; writing a
    // zeroed/partial block 0 through the gen1a backdoor would corrupt the
    // target's manufacturer block.
    bool is_magic = false;
    bool uid_source_ok = dump->sectors[0].readable;
    if (uid_source_ok && target_uid.length == dump->uid.length) {
        uint8_t block0[RC522_BLOCK_SIZE];
        memcpy(block0, dump->sectors[0].blocks[0], RC522_BLOCK_SIZE);
        rc522_gen1a_write_block0(block0, &is_magic);
    }

    display_clear();
    display_draw_text_color(0, 0, "RFID Clone", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Writing sectors...");
    display_flush();
    int written = clone_to_card(&target_uid, dump, /* write_trailers = */ false);
    if (written == 0) {
        diag_record_error("RFID Clone", "RC522_CLONE_WRITE_FAILED");
    }

    display_clear();
    display_draw_text_color(0, 0, "RFID Clone", DISPLAY_COLOR_ACCENT);
    char result_line[DISPLAY_COLS + 1];
    snprintf(result_line, sizeof(result_line), "%d/%d sectors cloned", written, dump->sectors_read);
    display_draw_text(2, 0, result_line);
    display_draw_text(3, 0, !uid_source_ok ? "UID not read; skipped"
                            : is_magic      ? "UID cloned (gen1a)"
                                            : "UID not cloned");
    display_draw_text(5, 0, "Trailers not written");
    display_draw_text(6, 0, "(data blocks only)");
    display_draw_text(7, 0, "Press any key");
    display_flush();

    wait_for_any_key();
    free(dump);
    rc522_antenna_off();
    menu_render(s_active_menu);
}

// Runs the scroll-keyboard text_entry_t loop with the given prompt line and
// returns true with entry->buffer filled once '^' (OK) is pressed, or false
// if BACK cancelled. Shared by action_ir_learn() and the RFID library save
// actions below -- all three are "scan/capture -> name it -> save" flows
// that differ only in what they capture and where they save it.
static bool prompt_for_name(text_entry_t *entry, const char *prompt)
{
    text_entry_init(entry);
    for (;;) {
        text_entry_render(entry, prompt, /* mask = */ false);

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK) {
            return false;
        }
        if (text_entry_handle_button(entry, event)) {
            return true; // '^' (OK) was pressed
        }
    }
}

// Blocks (BACK cancels) waiting for one 125kHz tag via rdm6300_poll(), then
// names and saves its UID to the RFID library. Mirrors action_ir_learn()'s
// "capture -> name it -> save" shape; unlike RFID Clone above, this never
// reads or stores any card data beyond the UID -- see rfid_library.h.
static void action_rfid_save_125khz(void)
{
    rdm6300_id_t id;
    bool captured = false;
    for (;;) {
        display_clear();
        display_draw_text_color(0, 0, "Save 125kHz Tag", DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, "Present tag now");
        display_draw_text(6, 0, "BACK: cancel");
        display_flush();

        for (int i = 0; i < 20 && !captured; i++) { // ~200ms between redraws
            button_id_t event = buttons_poll();
            if (event == BUTTON_BACK) {
                menu_render(s_active_menu);
                return;
            }
            if (rdm6300_poll(&id)) {
                captured = true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (captured) {
            break;
        }
    }

    vibration_pulse(80);

    text_entry_t entry;
    bool cancelled = !prompt_for_name(&entry, "Name this tag");

    display_clear();
    display_draw_text_color(0, 0, "Save 125kHz Tag", DISPLAY_COLOR_ACCENT);
    if (cancelled) {
        display_draw_text_color(2, 0, "Cancelled", DISPLAY_COLOR_DIM);
    } else if (entry.length == 0) {
        display_draw_text_color(2, 0, "Name can't be empty", DISPLAY_COLOR_ERROR);
    } else if (!rfid_library_add_125khz(entry.buffer, &id)) {
        display_draw_text_color(2, 0, "Library full", DISPLAY_COLOR_ERROR);
        display_draw_text(3, 0, "Delete one first");
        diag_record_error("RFID Save", "RFID_LIBRARY_FULL");
    } else {
        rfid_library_save(); // best-effort, same as ir_library_save()
        display_draw_text_color(2, 0, "Saved!", DISPLAY_COLOR_OK);
    }
    display_draw_text(6, 0, "Press any key");
    display_flush();

    wait_for_any_key();
    menu_render(s_active_menu);
}

// Same shape as action_rfid_save_125khz(), but for a 13.56MHz UID via
// wait_for_card() (the same helper RFID Clone uses to wait for a card).
// Despite rfid_library_entry_t's rc522_uid_t carrying a .length field wide
// enough for a 7/10-byte UID, rc522_read_uid() only ever produces a 4-byte
// one (cascade level 1 only -- see rc522.c) and reports anything longer as
// RC522_SCAN_UNSUPPORTED_UID, which wait_for_card() now surfaces as an
// explicit "not supported" result instead of hanging -- see its comment and
// KNOWN_ISSUES.md's Round 19 entry. So in practice only 4-byte UIDs ever
// reach here, the same as the clone/dump path.
static void action_rfid_save_1356mhz(void)
{
    rc522_antenna_on();

    rc522_uid_t uid;
    if (!wait_for_card("Save 13.56MHz Tag", "Present tag now", "RFID Save", &uid)) {
        rc522_antenna_off();
        menu_render(s_active_menu);
        return;
    }
    rc522_antenna_off();

    vibration_pulse(80);

    text_entry_t entry;
    bool cancelled = !prompt_for_name(&entry, "Name this tag");

    display_clear();
    display_draw_text_color(0, 0, "Save 13.56MHz Tag", DISPLAY_COLOR_ACCENT);
    if (cancelled) {
        display_draw_text_color(2, 0, "Cancelled", DISPLAY_COLOR_DIM);
    } else if (entry.length == 0) {
        display_draw_text_color(2, 0, "Name can't be empty", DISPLAY_COLOR_ERROR);
    } else if (!rfid_library_add_1356mhz(entry.buffer, &uid)) {
        display_draw_text_color(2, 0, "Library full", DISPLAY_COLOR_ERROR);
        display_draw_text(3, 0, "Delete one first");
        diag_record_error("RFID Save", "RFID_LIBRARY_FULL");
    } else {
        rfid_library_save(); // best-effort, same as ir_library_save()
        display_draw_text_color(2, 0, "Saved!", DISPLAY_COLOR_OK);
    }
    display_draw_text(6, 0, "Press any key");
    display_flush();

    wait_for_any_key();
    menu_render(s_active_menu);
}

// Browses saved RFID/NFC tags: UP/DOWN to select, LEFT to delete (entries
// shift up, oldest-first ordering same as ir_library.h). There is no
// PRESS:send here (unlike IR Library) -- RFID readers in this codebase are
// read-only inputs, there is nothing to "replay" a UID onto. BACK exits.
// Visible rows for a "header + scrolling list + 1 footer line" screen:
// row 0 is the header, the last row is the footer, everything between is
// list body. Shared by action_rfid_library()/action_ir_library()/
// action_error_history() below, all of which can hold more entries
// (RFID/IR: up to 16, Errors: up to DIAG_HISTORY_CAPACITY=24) than fit on
// screen at once -- see LIST_VISIBLE_ROWS's callers for the scroll-window
// logic this enables. Previously these three screens hardcoded their
// footer to row 7 while letting the list use rows 1..(DISPLAY_ROWS-2)
// (i.e. up to row 13) with no scroll window at all: past 7 entries the
// footer text got overwritten by list rows drawn on top of it, and once
// `selected`/`top` moved past what fit on screen the cursor became
// invisible with no way to tell where it was.
#define LIST_HEADER_ROWS 1
#define LIST_FOOTER_ROWS 1
#define LIST_VISIBLE_ROWS (DISPLAY_ROWS - LIST_HEADER_ROWS - LIST_FOOTER_ROWS)

// Keeps `top` (the index of the first visible row) such that `selected`
// stays within the LIST_VISIBLE_ROWS-tall window -- same clamping logic as
// menu.c's menu_clamp_scroll(), reimplemented here since these screens
// aren't menu_t-backed.
static void list_clamp_scroll(int selected, int *top)
{
    if (selected < *top) {
        *top = selected;
    } else if (selected >= *top + LIST_VISIBLE_ROWS) {
        *top = selected - LIST_VISIBLE_ROWS + 1;
    }
}

// Modal yes/no confirmation before a destructive, persisted action. Deletes
// in the library browsers used to be a single BUTTON_LEFT away, permanent,
// with no undo -- and LEFT is the ordinary "back" gesture everywhere else,
// so it was very easy to erase a saved record by reflex. Defaults to "no":
// the highlighted option is Cancel, PRESS/RIGHT confirms only after the user
// moves to Delete. Returns true only on an explicit Delete confirmation.
static bool confirm_delete(const char *item_name)
{
    bool on_delete = false; // start on Cancel
    for (;;) {
        display_clear();
        display_draw_text_color(0, 0, "Delete?", DISPLAY_COLOR_ERROR);
        display_draw_text(2, 0, item_name);
        display_draw_text_color(5, 0, on_delete ? "  Cancel" : "> Cancel",
                                on_delete ? DISPLAY_COLOR_TEXT : DISPLAY_COLOR_ACCENT);
        display_draw_text_color(6, 0, on_delete ? "> Delete" : "  Delete",
                                on_delete ? DISPLAY_COLOR_ERROR : DISPLAY_COLOR_TEXT);
        display_draw_text(DISPLAY_ROWS - 1, 0, "UP/DOWN PRESS BACK");
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK) {
            return false;
        } else if (event == BUTTON_UP || event == BUTTON_DOWN) {
            on_delete = !on_delete;
        } else if (event == BUTTON_PRESS || event == BUTTON_RIGHT) {
            return on_delete;
        }
    }
}

static void action_rfid_library(void)
{
    int selected = 0;
    int top = 0;
    for (;;) {
        int count = rfid_library_count();
        list_clamp_scroll(selected, &top);
        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "RFID Library (%d)", count);
        display_draw_text_color(0, 0, header, DISPLAY_COLOR_ACCENT);

        if (count == 0) {
            display_draw_text(2, 0, "No tags saved");
            display_draw_text(3, 0, "Use RFID Save first");
        } else {
            for (int i = 0; i < count - top && i < LIST_VISIBLE_ROWS; i++) {
                const rfid_library_entry_t *e = rfid_library_get(top + i);
                char line[DISPLAY_COLS + 1];
                char kind = (e->kind == RFID_LIBRARY_KIND_125KHZ) ? 'L' : 'H'; // Low/High freq
                snprintf(line, sizeof(line), "%c%c %.17s", (top + i == selected) ? '>' : ' ', kind, e->name);
                display_draw_text(LIST_HEADER_ROWS + i, 0, line);
            }
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, count > 0 ? "LEFT: delete" : "BACK: exit");
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK) {
            break;
        } else if (event == BUTTON_UP && selected > 0) {
            selected--;
        } else if (event == BUTTON_DOWN && selected < count - 1) {
            selected++;
        } else if (event == BUTTON_LEFT && count > 0) {
            const rfid_library_entry_t *e = rfid_library_get(selected);
            if (confirm_delete(e->name)) {
                rfid_library_remove(selected);
                rfid_library_save(); // best-effort, see action_ir_library()'s comment
                if (selected >= rfid_library_count() && selected > 0) {
                    selected--;
                }
            }
        }
    }

    menu_render(s_active_menu);
}

// Sends a fixed test frame -- a quick sanity check that the IR LED/RMT TX
// path works at all, independent of the library below.
// Sends a fixed test NEC frame and shows a brief confirmation before
// returning to the menu -- previously only logged via ESP_LOGI and
// returned immediately, giving a device-only user (no serial connection)
// no indication the button press was registered or that transmission was
// attempted. ir_driver_send() is void (no error return), so this can only
// confirm the request was made, not that the IR LED actually emitted --
// see KNOWN_ISSUES.md Round 25.
static void action_ir_send_test(void)
{
    ir_nec_frame_t frame = { .address = 0x00, .command = 0x45 };
    ESP_LOGI(TAG, "IR TX test frame: addr=0x%02X cmd=0x%02X", frame.address, frame.command);
    ir_driver_send(&frame);

    display_clear();
    display_draw_text_color(0, 0, "IR Send Test", DISPLAY_COLOR_ACCENT);
    display_draw_text_color(2, 0, "Test frame sent", DISPLAY_COLOR_OK);
    display_draw_text(3, 0, "addr=0x00 cmd=0x45");
    display_draw_text(6, 0, "Press any key");
    display_flush();
    wait_for_any_key();
}

// Blocks (BACK cancels) waiting for one NEC frame via ir_driver_poll_rx(),
// then lets the user name it with the scroll keyboard and saves it to the
// library. Mirrors action_wifi_setup_manual()'s "scan/pick -> name it ->
// save" shape.
static void action_ir_learn(void)
{
    ir_nec_frame_t frame;
    bool captured = false;
    for (;;) {
        display_clear();
        display_draw_text_color(0, 0, "IR Learn", DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, "Point remote here");
        display_draw_text(3, 0, "and press a button");
        display_draw_text(6, 0, "BACK: cancel");
        display_flush();

        for (int i = 0; i < 20 && !captured; i++) { // ~200ms between redraws
            button_id_t event = buttons_poll();
            if (event == BUTTON_BACK) {
                menu_render(s_active_menu);
                return;
            }
            if (ir_driver_poll_rx(&frame)) {
                captured = true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (captured) {
            break;
        }
    }

    vibration_pulse(80);

    text_entry_t entry;
    bool cancelled = !prompt_for_name(&entry, "Name this code");

    display_clear();
    display_draw_text_color(0, 0, "IR Learn", DISPLAY_COLOR_ACCENT);
    if (cancelled) {
        display_draw_text_color(2, 0, "Cancelled", DISPLAY_COLOR_DIM);
    } else if (entry.length == 0) {
        display_draw_text_color(2, 0, "Name can't be empty", DISPLAY_COLOR_ERROR);
    } else if (!ir_library_add(entry.buffer, &frame)) {
        display_draw_text_color(2, 0, "Library full", DISPLAY_COLOR_ERROR);
        display_draw_text(3, 0, "Delete one first");
        diag_record_error("IR Learn", "IR_LIBRARY_FULL");
    } else {
        ir_library_save(); // best-effort, same as diag_save() -- RAM copy is authoritative regardless
        display_draw_text_color(2, 0, "Saved!", DISPLAY_COLOR_OK);
    }
    display_draw_text(6, 0, "Press any key");
    display_flush();

    wait_for_any_key();
    menu_render(s_active_menu);
}

// Browses saved IR codes: UP/DOWN to select, PRESS to transmit the
// selected code, LEFT to delete it (with the ring buffer's "oldest
// first" ordering from ir_library.h -- entries shift up after a delete).
// BACK exits.
static void action_ir_library(void)
{
    int selected = 0;
    int top = 0;
    for (;;) {
        int count = ir_library_count();
        list_clamp_scroll(selected, &top);
        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "IR Library (%d)", count);
        display_draw_text_color(0, 0, header, DISPLAY_COLOR_ACCENT);

        if (count == 0) {
            display_draw_text(2, 0, "No codes saved");
            display_draw_text(3, 0, "Use IR Learn first");
        } else {
            for (int i = 0; i < count - top && i < LIST_VISIBLE_ROWS; i++) {
                const ir_library_entry_t *e = ir_library_get(top + i);
                char line[DISPLAY_COLS + 1];
                snprintf(line, sizeof(line), "%c%.19s", (top + i == selected) ? '>' : ' ', e->name);
                display_draw_text(LIST_HEADER_ROWS + i, 0, line);
            }
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, count > 0 ? "PRESS:send LEFT:del" : "BACK: exit");
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK) {
            break;
        } else if (event == BUTTON_UP && selected > 0) {
            selected--;
        } else if (event == BUTTON_DOWN && selected < count - 1) {
            selected++;
        } else if (event == BUTTON_PRESS && count > 0) {
            const ir_library_entry_t *e = ir_library_get(selected);
            ir_driver_send(&e->frame);
            vibration_pulse(80);
        } else if (event == BUTTON_LEFT && count > 0) {
            const ir_library_entry_t *e = ir_library_get(selected);
            if (confirm_delete(e->name)) {
                ir_library_remove(selected);
                ir_library_save(); // best-effort, see action_ir_learn()'s comment
                if (selected >= ir_library_count() && selected > 0) {
                    selected--;
                }
            }
        }
    }

    menu_render(s_active_menu);
}

// Opens the optional four-receiver direction screen. This C6-Pico hardware
// profile has only the primary IR receiver and does not initialize the
// direction array, so the screen explicitly reports that it is unavailable.
static void action_ir_direction_find(void)
{
    s_screen = APP_SCREEN_IR_DIRECTION;
    s_last_scan_line[0] = '\0';
    s_screen_dirty = true;
}

// Blocks for a few seconds while the C6 scans, then shows the result on
// screen -- previously only logged via ESP_LOGI/ESP_LOGW and returned
// immediately with no on-device indication a scan happened, how many APs
// were found, or why it failed (a device-only user with no serial
// connection saw nothing at all; see KNOWN_ISSUES.md Round 25 and
// HARDWARE_TEST_MATRIX.md's "Wi-Fi Scan Test" row, which expects a real
// AP list to be visible). Replace with a real "browse networks" screen
// (like action_wifi_setup_manual()'s picker) once there's a UI for
// picking one to connect to -- this is still just a result display, no
// selection.
static void action_wifi_scan_test(void)
{
    // "[STA]" marks this as a station-mode scan (normal client behavior,
    // same as a phone listing nearby Wi-Fi) so it's visually distinct from
    // "[MON]" (promiscuous Wi-Fi Monitor, below) -- the two use different
    // radio modes and the difference matters (Monitor drops any STA
    // connection; a station scan doesn't).
    display_clear();
    display_draw_text_color(0, 0, "WiFi Scan Test [STA]", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Scanning...");
    display_flush();

    c6_network_t networks[C6_MAX_NETWORKS];
    int count = c6_link_scan(networks, C6_MAX_NETWORKS);

    display_clear();
    display_draw_text_color(0, 0, "WiFi Scan Test [STA]", DISPLAY_COLOR_ACCENT);
    if (count < 0) {
        ESP_LOGW(TAG, "Wi-Fi scan failed (C6 not responding?)");
        diag_record_error("WiFi Scan Test", "C6_LINK_SCAN_FAILED");
        display_draw_text_color(2, 0, "Scan failed", DISPLAY_COLOR_ERROR);
        display_draw_text(3, 0, "C6 not responding?");
    } else if (count == 0) {
        display_draw_text_color(2, 0, "No networks found", DISPLAY_COLOR_DIM);
    } else {
        ESP_LOGI(TAG, "Wi-Fi scan found %d network(s):", count);
        char summary[DISPLAY_COLS + 1];
        snprintf(summary, sizeof(summary), "%d network(s) found:", count);
        display_draw_text(1, 0, summary);
        // LIST_VISIBLE_ROWS rows available below the header+summary lines;
        // this is a one-shot result display (not a scrollable picker like
        // action_wifi_setup_manual()'s), so anything past that is just
        // noted rather than scrolled to.
        int shown = count < LIST_VISIBLE_ROWS - 1 ? count : LIST_VISIBLE_ROWS - 1;
        for (int i = 0; i < shown; i++) {
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "%.15s (%d dBm)", networks[i].ssid, networks[i].rssi);
            ESP_LOGI(TAG, "  %s (%d dBm)", networks[i].ssid, networks[i].rssi);
            display_draw_text(2 + i, 0, line);
        }
        for (int i = shown; i < count; i++) {
            ESP_LOGI(TAG, "  %s (%d dBm)", networks[i].ssid, networks[i].rssi);
        }
        if (count > shown) {
            // The truncation notice shares the bottom row with the footer;
            // drawing it on its own row (2 + shown) previously collided with
            // the "Press any key" line and was never visible. Merge them.
            char more[DISPLAY_COLS + 1];
            snprintf(more, sizeof(more), "+%d more - any key", count - shown);
            display_draw_text_color(DISPLAY_ROWS - 1, 0, more, DISPLAY_COLOR_DIM);
        } else {
            display_draw_text(DISPLAY_ROWS - 1, 0, "Press any key");
        }
        display_flush();
        wait_for_any_key();
        return;
    }
    display_draw_text(DISPLAY_ROWS - 1, 0, "Press any key");
    display_flush();
    wait_for_any_key();
}

static void action_wifi_setup(void)
{
    display_clear();
    display_draw_text_color(0, 0, "WiFi Setup", DISPLAY_COLOR_ACCENT);
    display_draw_text_color(2, 0, "Not available", DISPLAY_COLOR_DIM);
    display_draw_text(4, 0, "Use Setup Manual");
    display_draw_text(6, 0, "Press any key");
    display_flush();

    wait_for_any_key();
    menu_render(s_active_menu);
}

// No-phone fallback: scan, pick a network with UP/DOWN/PRESS, then type
// the password on the joystick-driven scroll keyboard. Fully blocking,
// same as the other setup actions -- there's no other screen to interrupt
// this with anyway.
static void action_wifi_setup_manual(void)
{
    // "[STA]" for the same reason as action_wifi_scan_test() above -- this
    // whole flow (scan, pick, connect) is station mode end to end.
    display_clear();
    display_draw_text_color(0, 0, "WiFi Setup [STA]", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Scanning...");
    display_flush();

    c6_network_t networks[C6_MAX_NETWORKS];
    int count = c6_link_scan(networks, C6_MAX_NETWORKS);
    if (count < 0) {
        diag_record_error("WiFi Setup Manual", "C6_LINK_SCAN_FAILED");
        display_clear();
        display_draw_text_color(0, 0, "Scan failed", DISPLAY_COLOR_ERROR);
        display_draw_text(1, 0, "Radio unavailable");
        display_draw_text(2, 0, "Press any key");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }
    if (count == 0) {
        display_clear();
        display_draw_text_color(0, 0, "No networks found", DISPLAY_COLOR_DIM);
        display_draw_text(2, 0, "Press any key");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }

    // --- Network picker: UP/DOWN moves, PRESS selects, LEFT cancels. ---
    // count can be up to C6_MAX_NETWORKS (16), more than fit in
    // DISPLAY_ROWS-1 (14) rows below the header -- scroll the window with
    // `top` the same way action_rfid_library()/action_ir_library() do, so
    // `selected` never lands on a row that isn't drawn.
    int selected = 0;
    int top = 0;
    bool picked = false;
    bool cancelled = false;
    for (;;) {
        if (selected < top) {
            top = selected;
        } else if (selected >= top + (DISPLAY_ROWS - 1)) {
            top = selected - (DISPLAY_ROWS - 1) + 1;
        }
        display_clear();
        display_draw_text_color(0, 0, "Pick a network:", DISPLAY_COLOR_ACCENT);
        for (int i = 0; i < count - top && i < DISPLAY_ROWS - 1; i++) {
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "%c%.20s", (top + i == selected) ? '>' : ' ', networks[top + i].ssid);
            display_draw_text(1 + i, 0, line);
        }
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_UP && selected > 0) {
            selected--;
        } else if (event == BUTTON_DOWN && selected < count - 1) {
            selected++;
        } else if (event == BUTTON_PRESS) {
            picked = true;
            break;
        } else if (event == BUTTON_BACK) {
            cancelled = true;
            break;
        }
    }

    if (cancelled || !picked) {
        menu_render(s_active_menu);
        return;
    }

    // --- Password entry via the scroll keyboard. ---
    text_entry_t entry;
    text_entry_init(&entry);
    char title[DISPLAY_COLS + 1];
    snprintf(title, sizeof(title), "Pwd for %.12s", networks[selected].ssid);

    bool entry_cancelled = false;
    for (;;) {
        text_entry_render(&entry, title, /* mask = */ true);

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK) {
            entry_cancelled = true;
            break;
        }
        if (text_entry_handle_button(&entry, event)) {
            break; // '^' (OK) was pressed
        }
    }

    if (entry_cancelled) {
        menu_render(s_active_menu);
        return;
    }

    display_clear();
    display_draw_text_color(0, 0, "Connecting...", DISPLAY_COLOR_ACCENT);
    display_flush();

    bool ok = c6_link_connect(networks[selected].ssid, entry.buffer);
    if (!ok) {
        diag_record_error("WiFi Setup Manual", "C6_LINK_CONNECT_FAILED");
    }

    display_clear();
    display_draw_text_color(0, 0, "WiFi Setup", DISPLAY_COLOR_ACCENT);
    display_draw_text_color(2, 0, ok ? "Connected!" : "Failed",
                             ok ? DISPLAY_COLOR_OK : DISPLAY_COLOR_ERROR);
    display_draw_text(6, 0, "Press any key");
    display_flush();

    wait_for_any_key();

    menu_render(s_active_menu);
}

// Passive Wi-Fi Monitor: starts the C6's promiscuous channel-hop sniffer
// (see c6_link.h's c6_link_monitor_start() comment) and shows a live,
// polled list of APs seen (SSID/channel/RSSI) until BACK. Fully self-
// contained blocking loop, same shape as action_wifi_setup_manual()'s
// network picker, except it never exits on its own -- only BACK stops it.
//
// Starting this disconnects the C6's STA connection and blocks every other
// C6 feature (WiFi Scan/Setup, Errors -> Send) for as long as the screen
// is open -- see c6_link.h. That's called out on-screen so it isn't a
// surprise, and c6_link_monitor_stop() is called on every exit path
// (BACK, or the start failing) so nothing can leave the C6 stuck in
// monitor mode after the user backs out.
static void action_wifi_monitor(void)
{
    // "[MON]" marks this as promiscuous Wi-Fi Monitor mode, the C6's radio
    // in receive-everything mode rather than station mode -- distinct
    // enough (drops the STA connection, can't scan/connect while running)
    // that the screen should say so at a glance, not just via the menu
    // label. See action_wifi_scan_test()'s "[STA]" comment for the
    // matching station-mode marker.
    display_clear();
    display_draw_text_color(0, 0, "WiFi Monitor [MON]", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Starting...");
    display_draw_text_color(4, 0, "Drops STA connection", DISPLAY_COLOR_DIM);
    display_flush();

    if (!c6_link_monitor_start()) {
        diag_record_error("WiFi Monitor", "C6_LINK_MONITOR_START_FAILED");
        display_clear();
        display_draw_text_color(0, 0, "WiFi Monitor [MON]", DISPLAY_COLOR_ACCENT);
        display_draw_text_color(2, 0, "Failed to start", DISPLAY_COLOR_ERROR);
        display_draw_text(6, 0, "Press any key");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }

    c6_monitor_ap_t aps[C6_MONITOR_MAX_APS];
    for (;;) {
        int count = c6_link_monitor_poll(aps, C6_MONITOR_MAX_APS);

        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "WiFi Mon [MON] (%d)", count);
        display_draw_text_color(0, 0, header, DISPLAY_COLOR_ACCENT);
        // count can be up to C6_MONITOR_MAX_APS (32); only the first
        // LIST_VISIBLE_ROWS fit without drawing over the footer below (this
        // list has no scroll -- it's a live, order-shifting feed, not
        // something to page through with a cursor).
        for (int i = 0; i < count && i < LIST_VISIBLE_ROWS; i++) {
            char line[DISPLAY_COLS + 1];
            // Clamp every text field so the largest channel/RSSI values
            // plus the terminator always fit within DISPLAY_COLS. Vendor
            // is a short manufacturer label derived from the BSSID's OUI
            // (see oui_vendor_lookup() in c6_link.c), "?" if unrecognized.
            snprintf(line, sizeof(line), "%.6s %.4s c%u %d %.6s",
                     aps[i].ssid[0] ? aps[i].ssid : "(hid)",
                     aps[i].sec[0] ? aps[i].sec : "?",
                     aps[i].channel, aps[i].rssi,
                     aps[i].vendor[0] ? aps[i].vendor : "?");
            display_draw_text(LIST_HEADER_ROWS + i, 0, line);
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, "BACK: stop+exit");
        display_flush();

        bool stop = false;
        for (int i = 0; i < 50 && !stop; i++) { // ~500ms between list refreshes
            if (buttons_poll() == BUTTON_BACK) {
                stop = true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (stop) {
            break;
        }
    }

    c6_link_monitor_stop();
    menu_render(s_active_menu);
}

// Passive BT Scan: same shape as action_wifi_monitor() immediately above
// (see c6_link.h's c6_link_bt_scan_start() comment). The standalone build
// keeps BLE discovery and promiscuous Wi-Fi monitor mutually exclusive.
// Receive-only: lists BLE devices seen (address/name/RSSI), never
// connects to anything.
static void action_bt_scan(void)
{
    display_clear();
    display_draw_text_color(0, 0, "BT Scan", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Starting...");
    display_flush();

    if (!c6_link_bt_scan_start()) {
        diag_record_error("BT Scan", "C6_LINK_BT_SCAN_START_FAILED");
        display_clear();
        display_draw_text_color(0, 0, "BT Scan", DISPLAY_COLOR_ACCENT);
        display_draw_text_color(2, 0, "Failed to start", DISPLAY_COLOR_ERROR);
        display_draw_text(6, 0, "Press any key");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }

    c6_bt_device_t devices[C6_BT_MAX_DEVICES];
    for (;;) {
        int count = c6_link_bt_scan_poll(devices, C6_BT_MAX_DEVICES);

        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "BT Scan (%d)", count);
        display_draw_text_color(0, 0, header, DISPLAY_COLOR_ACCENT);
        // Same LIST_VISIBLE_ROWS clamp as action_wifi_monitor() above --
        // count can be up to C6_BT_MAX_DEVICES (32).
        for (int i = 0; i < count && i < LIST_VISIBLE_ROWS; i++) {
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "%.13s %ddBm",
                     devices[i].name[0] ? devices[i].name : "(no name)", devices[i].rssi);
            display_draw_text(LIST_HEADER_ROWS + i, 0, line);
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, "BACK: stop+exit");
        display_flush();

        bool stop = false;
        for (int i = 0; i < 50 && !stop; i++) { // ~500ms between list refreshes
            if (buttons_poll() == BUTTON_BACK) {
                stop = true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (stop) {
            break;
        }
    }

    c6_link_bt_scan_stop();
    menu_render(s_active_menu);
}

static void action_about(void)
{
    display_clear();
    display_draw_text_color(0, 0, DEVICE_NAME, DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "DIY multi-tool");
    display_draw_text(3, 0, "ESP32-C6-Pico 4MB");
    display_draw_text(5, 0, "RFID / NFC (13.56 & 125k)");
    display_draw_text(6, 0, "Infrared TX/RX + learn");
    display_draw_text(7, 0, "WiFi scan/setup/monitor");
    display_draw_text(8, 0, "Bluetooth LE scan");
    display_draw_text_color(DISPLAY_ROWS - 2, 0, "github.com/ErdemWilkinson", DISPLAY_COLOR_DIM);
    display_draw_text_color(DISPLAY_ROWS - 1, 0, "Press any key...", DISPLAY_COLOR_DIM);
    display_flush();
    wait_for_any_key();
}

// Formats how long ago `timestamp_us` (an esp_timer_get_time() value) was,
// relative to now -- boot-relative, not wall-clock (no RTC on this
// device), so this is only meaningful within the current boot session.
static void format_relative_time(char *out, size_t out_cap, int64_t timestamp_us)
{
    int64_t age_s = (esp_timer_get_time() - timestamp_us) / 1000000;
    if (age_s < 60) {
        snprintf(out, out_cap, "%llds ago", (long long)age_s);
    } else if (age_s < 3600) {
        snprintf(out, out_cap, "%lldm ago", (long long)(age_s / 60));
    } else {
        snprintf(out, out_cap, "%lldh ago", (long long)(age_s / 3600));
    }
}

// Shows the device's local error history (main/diag/diag.h), newest
// first: module/code/relative-age per line, UP/DOWN to scroll one entry
// at a time and BACK to exit. Network upload is not part of this standalone
// build. This is a one-time snapshot taken when the screen opens, not
// live-polled -- errors recorded while this screen is open won't appear
// until it is reopened.
static void action_error_history(void)
{
    diag_entry_t entries[DIAG_HISTORY_CAPACITY];
    int count = diag_get_history(entries, DIAG_HISTORY_CAPACITY);

    int top = 0;
    for (;;) {
        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "Errors (%d)", count);
        display_draw_text_color(0, 0, header, DISPLAY_COLOR_ACCENT);

        if (count == 0) {
            display_draw_text(2, 0, "No errors recorded");
        } else {
            // Field widths widened to use the larger DISPLAY_COLS (was
            // 7/5/7 on the old 21-column OLED) -- still clamped with %.*s
            // rather than assuming module/code names fit, since diag.h's
            // DIAG_MODULE_MAX_LEN/DIAG_CODE_MAX_LEN allow longer names
            // than any single field here.
            for (int i = 0; i < count - top && i < LIST_VISIBLE_ROWS; i++) {
                const diag_entry_t *e = &entries[top + i];
                char ago[16];
                format_relative_time(ago, sizeof(ago), e->timestamp_us);
                char line[DISPLAY_COLS + 1];
                snprintf(line, sizeof(line), "%.12s %.9s %.7s", e->module, e->code, ago);
                display_draw_text(LIST_HEADER_ROWS + i, 0, line);
            }
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, "BACK: exit");
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK) {
            break;
        }
        if (event == BUTTON_UP && top > 0) {
            top--;
        } else if (event == BUTTON_DOWN && top < count - LIST_VISIBLE_ROWS) {
            // Stop once the last row is on screen -- previously this
            // allowed `top` up to count-1, which (now that the list only
            // shows LIST_VISIBLE_ROWS rows instead of drawing over the
            // footer) would scroll a mostly-blank page into view with
            // only the very last entry showing at the top.
            top++;
        }
    }

    // Persist on exit rather than on every diag_record_error() -- see
    // diag.h's comment on why saves are explicit/caller-driven. Failure is
    // silently ignored: the on-screen history the user just looked at is
    // already correct regardless of whether the NVS write succeeded.
    diag_save();

    menu_render(s_active_menu);
}

// --- Menu tree ---------------------------------------------------------
// Top level is a set of categories; each opens its own flat submenu.
// LEFT backs out of a submenu to its parent (menu_link_submenu() below
// wires that up); BACK (a separate physical button, not part of the menu
// at all) exits whatever screen/action is active straight back to
// whichever menu launched it, handled in the main loop / actions above.

static menu_item_t s_rfid_menu_items[] = {
    {"Read 125kHz",   action_rfid_125khz, NULL},
    {"Read 13.56MHz", action_nfc_1356mhz, NULL},
    {"Clone (13.56MHz)", action_rfid_clone, NULL},
    {"Save 125kHz",    action_rfid_save_125khz,  NULL},
    {"Save 13.56MHz",  action_rfid_save_1356mhz, NULL},
    {"RFID Library",   action_rfid_library,      NULL},
};

static menu_item_t s_ir_menu_items[] = {
    {"IR Send Test",     action_ir_send_test,      NULL},
    {"IR Learn",          action_ir_learn,          NULL},
    {"IR Library",        action_ir_library,        NULL},
    // (N/A): unavailable on this hardware profile, not a bug -- see the
    // action's own comment and KNOWN_ISSUES.md's Round 27. Marked in the
    // label itself so the menu doesn't present a dead control as a normal
    // one; selecting it still shows the full explanation on-screen.
    {"IR Direction Find (N/A)", action_ir_direction_find, NULL},
};

static menu_item_t s_wifi_menu_items[] = {
    {"WiFi Scan Test",     action_wifi_scan_test,   NULL},
    // (N/A): c6_link_setup() is a stub on this build (see its comment) --
    // Wi-Fi Setup Manual below is the working join path. Marked in the
    // label for the same reason as "IR Direction Find (N/A)" above.
    {"WiFi Setup (N/A)",    action_wifi_setup,       NULL},
    {"WiFi Setup Manual",   action_wifi_setup_manual, NULL},
    {"WiFi Monitor",        action_wifi_monitor,     NULL},
};

static menu_item_t s_bluetooth_menu_items[] = {
    {"BT Scan", action_bt_scan, NULL},
};

// Indices [0..3] below must stay in sync with the menu_link_submenu()
// calls in app_main() -- reordering these items without updating those
// calls (or their hardcoded indices) makes the moved category silently
// do nothing when selected, with no compiler warning.
static menu_item_t s_main_menu_items[] = {
    {"RFID / NFC", NULL, NULL},
    {"Infrared",   NULL, NULL},
    {"WiFi",       NULL, NULL},
    {"Bluetooth",  NULL, NULL},
    {"Errors",     action_error_history, NULL},
    {"About",      action_about, NULL},
};

static menu_t s_main_menu;
static menu_t s_rfid_menu;
static menu_t s_ir_menu;
static menu_t s_wifi_menu;
static menu_t s_bluetooth_menu;

static void render_scan_screen(const char *title)
{
    display_clear();
    display_draw_text_color(0, 0, title, DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, s_last_scan_line[0] ? s_last_scan_line : "Scanning...");
    display_draw_text(6, 0, "BACK button: exit");
    display_flush();
}

static void render_ir_direction_screen(uint8_t flags)
{
    display_clear();
    display_draw_text_color(0, 0, "IR Direction Find", DISPLAY_COLOR_ACCENT);
    if (!ir_direction_is_available()) {
        // ir_direction_init() isn't called on this build (see
        // KNOWN_ISSUES.md's Round 13/15) -- say so explicitly rather than
        // leaving the user on a "Waiting for IR..." screen that can never
        // report anything.
        display_draw_text_color(2, 0, "Not available", DISPLAY_COLOR_ERROR);
        display_draw_text(3, 0, "on this build");
        display_draw_text(5, 0, "(RMT channels used");
        display_draw_text(6, 0, "by IR RX/TX already)");
        display_draw_text(8, 0, "BACK button: exit");
        display_flush();
        return;
    }
    if (s_last_scan_line[0]) {
        display_draw_text(2, 0, s_last_scan_line);
    } else {
        display_draw_text(2, 0, "Waiting for IR...");
    }
    char dirs[DISPLAY_COLS + 1];
    snprintf(dirs, sizeof(dirs), "%c%c%c%c",
             (flags & IR_DIR_NORTH) ? 'N' : '-',
             (flags & IR_DIR_EAST)  ? 'E' : '-',
             (flags & IR_DIR_SOUTH) ? 'S' : '-',
             (flags & IR_DIR_WEST)  ? 'W' : '-');
    display_draw_text(4, 0, dirs);
    display_draw_text(6, 0, "BACK button: exit");
    display_flush();
}

void app_main(void)
{
    // NVS backs diag_load()/diag_save(), ir_library_load()/_save(), and
    // rfid_library_load()/_save() (see diag.h, ir_library.h, rfid_library.h)
    // -- erase-and-retry on the two "partition needs reformatting" error
    // codes, same pattern the C6 side already uses in c6-firmware/main/main.c.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    diag_load();
    ir_library_load();
    rfid_library_load();

    display_init();
    render_boot_splash();
    buttons_init();
    ir_driver_init();
    // The four-receiver direction finder is retained for a future hardware
    // profile but is not initialized on this fixed C6-Pico pin map.
    rc522_init();
    // TEMPORARILY DISABLED: first real-hardware bring-up found that
    // uart_set_pin(..., UART_RX_GPIO=16, ...) inside rdm6300_init() hangs
    // and trips the interrupt watchdog (Guru Meditation, "Interrupt wdt
    // timeout on CPU0") on this ESP32-C6-MINI-1 board -- confirmed by a
    // diagnostic ESP_LOGI() right before/after the call: the "before" line
    // printed, the "after" line never did. GPIO16 likely collides with the
    // MINI-1 module's internal flash/PSRAM QSPI pins on this specific
    // board revision -- see KNOWN_ISSUES.md's real-hardware bring-up entry.
    // Re-enable once RDM6300 RX is rewired to a confirmed-free GPIO (the
    // hardware plan's spare pins, e.g. GP7/GP28) and UART_RX_GPIO in
    // rdm6300.c is updated to match; do NOT just uncomment this on GPIO16.
    // rdm6300_init();
    vibration_init();
    c6_link_init();

    menu_init(&s_main_menu, s_main_menu_items,
              sizeof(s_main_menu_items) / sizeof(s_main_menu_items[0]));
    menu_init(&s_rfid_menu, s_rfid_menu_items,
              sizeof(s_rfid_menu_items) / sizeof(s_rfid_menu_items[0]));
    menu_init(&s_ir_menu, s_ir_menu_items,
              sizeof(s_ir_menu_items) / sizeof(s_ir_menu_items[0]));
    menu_init(&s_wifi_menu, s_wifi_menu_items,
              sizeof(s_wifi_menu_items) / sizeof(s_wifi_menu_items[0]));
    menu_init(&s_bluetooth_menu, s_bluetooth_menu_items,
              sizeof(s_bluetooth_menu_items) / sizeof(s_bluetooth_menu_items[0]));

    menu_link_submenu(&s_main_menu, &s_main_menu_items[0], &s_rfid_menu);
    menu_link_submenu(&s_main_menu, &s_main_menu_items[1], &s_ir_menu);
    menu_link_submenu(&s_main_menu, &s_main_menu_items[2], &s_wifi_menu);
    menu_link_submenu(&s_main_menu, &s_main_menu_items[3], &s_bluetooth_menu);

    // Catches a forgotten/misindexed menu_link_submenu() call above at
    // boot (see KNOWN_ISSUES.md) instead of leaving a menu item that
    // silently does nothing when a user eventually selects it. Only
    // s_main_menu has category items (NULL on_select, wired to a
    // submenu) -- the rest are flat leaf-item menus, safe by construction,
    // so they don't need this check.
    menu_assert_fully_wired(&s_main_menu);

    s_active_menu = &s_main_menu;
    menu_render(s_active_menu);

    while (1) {
        button_id_t event = buttons_poll();

        if (s_screen == APP_SCREEN_MENU) {
            bool needs_render = false;
            if (event != BUTTON_COUNT) {
                menu_t *next = menu_handle_button(s_active_menu, event);
                if (next != s_active_menu) {
                    s_active_menu = next;
                }
                needs_render = true;
            }
            if (s_active_menu->anim_offset_px != 0) {
                menu_animate_tick(s_active_menu);
                needs_render = true;
            }
            if (needs_render) {
                menu_render(s_active_menu);
            }
        } else if (event == BUTTON_BACK) {
            if (s_screen == APP_SCREEN_SCAN_1356MHZ) {
                rc522_antenna_off();
            }
            s_screen = APP_SCREEN_MENU;
            menu_render(s_active_menu);
        } else if (s_screen == APP_SCREEN_IR_DIRECTION) {
            uint8_t flags = 0;
            ir_nec_frame_t frame;
            if (ir_direction_poll(&flags, &frame)) {
                snprintf(s_last_scan_line, sizeof(s_last_scan_line),
                         "addr=0x%02X cmd=0x%02X", frame.address, frame.command);
                vibration_pulse(80);
                s_screen_dirty = true;
            }
            if (s_screen_dirty || flags != 0) {
                render_ir_direction_screen(flags);
                s_screen_dirty = false;
            }
        } else {
            const char *title = (s_screen == APP_SCREEN_SCAN_125KHZ) ? "125kHz RFID" : "13.56MHz NFC";
            // True only on the poll that transitions "nothing in the field"
            // -> "tag present" -- vibration/diag firing is gated on this,
            // not on merely reading a tag again this tick (see
            // s_scan_card_present's comment above).
            bool new_presentation = false;

            if (s_screen == APP_SCREEN_SCAN_125KHZ) {
                rdm6300_id_t id;
                if (rdm6300_poll(&id)) {
                    s_rdm6300_miss_streak = 0;
                    if (!s_scan_card_present) {
                        s_scan_card_present = true;
                        new_presentation = true;
                    }
                    snprintf(s_last_scan_line, sizeof(s_last_scan_line),
                             "%02X%02X%02X%02X%02X",
                             id.bytes[0], id.bytes[1], id.bytes[2], id.bytes[3], id.bytes[4]);
                } else if (s_scan_card_present) {
                    if (++s_rdm6300_miss_streak >= RDM6300_ABSENCE_POLLS) {
                        s_scan_card_present = false;
                        s_rdm6300_miss_streak = 0;
                    }
                }
            } else {
                rc522_uid_t uid;
                rc522_scan_result_t result = rc522_read_uid(&uid);
                if (result != RC522_SCAN_ERROR) {
                    s_rc522_scan_error_latched = false;
                }
                if (result == RC522_SCAN_NO_CARD) {
                    s_scan_card_present = false;
                } else if (result == RC522_SCAN_OK) {
                    if (!s_scan_card_present) {
                        s_scan_card_present = true;
                        new_presentation = true;
                    }
                    int n = 0;
                    for (int i = 0; i < uid.length && n < DISPLAY_COLS - 3; i++) {
                        n += snprintf(&s_last_scan_line[n], sizeof(s_last_scan_line) - n,
                                      "%02X ", uid.bytes[i]);
                    }
                } else if (result == RC522_SCAN_UNSUPPORTED_UID) {
                    if (!s_scan_card_present) {
                        s_scan_card_present = true;
                        new_presentation = true;
                        diag_record_error("13.56MHz NFC", "RC522_SCAN_UNSUPPORTED_UID");
                    }
                    snprintf(s_last_scan_line, sizeof(s_last_scan_line),
                             "7/10-byte UID: N/A");
                } else if (result == RC522_SCAN_ERROR) {
                    if (!s_rc522_scan_error_latched) {
                        s_rc522_scan_error_latched = true;
                        diag_record_error("13.56MHz NFC", "RC522_SCAN_ERROR");
                    }
                }
            }

            if (new_presentation) {
                vibration_pulse(80);
                s_screen_dirty = true;
            }
            if (s_screen_dirty) {
                render_scan_screen(title);
                s_screen_dirty = false;
            }
        }

        // IR RX runs in the background regardless of which screen is
        // showing; for now just log what came in. A dedicated "Read IR"
        // screen (like the RFID ones) can consume this instead once it exists.
        ir_nec_frame_t rx_frame;
        if (ir_driver_poll_rx(&rx_frame)) {
            ESP_LOGI(TAG, "IR RX: addr=0x%02X cmd=0x%02X", rx_frame.address, rx_frame.command);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
