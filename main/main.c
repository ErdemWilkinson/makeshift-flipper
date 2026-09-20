#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
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

static void action_rfid_125khz(void)
{
    s_screen = APP_SCREEN_SCAN_125KHZ;
    s_last_scan_line[0] = '\0';
    s_screen_dirty = true;
}

static void action_nfc_1356mhz(void)
{
    s_screen = APP_SCREEN_SCAN_1356MHZ;
    s_last_scan_line[0] = '\0';
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

// Waits (blocking, antenna must already be on) for any card and returns its
// UID. Used by both the "scan source" and "place target card" steps of
// dump/clone -- polls at the same ~10ms cadence as the main loop's own scan
// screen. BACK cancels and returns false.
static bool wait_for_card(const char *prompt_line, rc522_uid_t *out_uid)
{
    for (;;) {
        display_clear();
        display_draw_text_color(0, 0, "RFID Clone", DISPLAY_COLOR_ACCENT);
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
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

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
    rc522_card_dump_t *dump = malloc(sizeof(rc522_card_dump_t));
    if (dump == NULL) {
        diag_record_error("RFID Clone", "RC522_CLONE_OUT_OF_MEMORY");
        rc522_antenna_off();
        menu_render(s_active_menu);
        return;
    }

    if (!wait_for_card("Place source card", &dump->uid)) {
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
    if (!wait_for_card("Place TARGET card", &target_uid)) {
        free(dump);
        rc522_antenna_off();
        menu_render(s_active_menu);
        return;
    }

    // UID clone is a separate, best-effort pass via the gen1a backdoor --
    // most targets won't be magic cards, and that's fine, data cloning
    // below doesn't depend on it.
    bool is_magic = false;
    if (target_uid.length == dump->uid.length) {
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
    display_draw_text(3, 0, is_magic ? "UID cloned (gen1a)" : "UID not cloned");
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
// A 7/10-byte UID is stored as-is -- rfid_library_entry_t's rc522_uid_t
// already carries .length for those, unlike the clone/dump path which
// only supports 4-byte UIDs.
static void action_rfid_save_1356mhz(void)
{
    rc522_antenna_on();

    rc522_uid_t uid;
    if (!wait_for_card("Present tag now", &uid)) {
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
static void action_rfid_library(void)
{
    int selected = 0;
    for (;;) {
        int count = rfid_library_count();
        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "RFID Library (%d)", count);
        display_draw_text_color(0, 0, header, DISPLAY_COLOR_ACCENT);

        if (count == 0) {
            display_draw_text(2, 0, "No tags saved");
            display_draw_text(3, 0, "Use RFID Save first");
        } else {
            for (int i = 0; i < count && i < DISPLAY_ROWS - 2; i++) {
                const rfid_library_entry_t *e = rfid_library_get(i);
                char line[DISPLAY_COLS + 1];
                char kind = (e->kind == RFID_LIBRARY_KIND_125KHZ) ? 'L' : 'H'; // Low/High freq
                snprintf(line, sizeof(line), "%c%c %.17s", (i == selected) ? '>' : ' ', kind, e->name);
                display_draw_text(1 + i, 0, line);
            }
        }
        display_draw_text(7, 0, count > 0 ? "LEFT: delete" : "BACK: exit");
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
            rfid_library_remove(selected);
            rfid_library_save(); // best-effort, see action_ir_library()'s comment
            if (selected >= rfid_library_count() && selected > 0) {
                selected--;
            }
        }
    }

    menu_render(s_active_menu);
}

// Sends a fixed test frame -- a quick sanity check that the IR LED/RMT TX
// path works at all, independent of the library below.
static void action_ir_send_test(void)
{
    ir_nec_frame_t frame = { .address = 0x00, .command = 0x45 };
    ESP_LOGI(TAG, "IR TX test frame: addr=0x%02X cmd=0x%02X", frame.address, frame.command);
    ir_driver_send(&frame);
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
    for (;;) {
        int count = ir_library_count();
        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "IR Library (%d)", count);
        display_draw_text_color(0, 0, header, DISPLAY_COLOR_ACCENT);

        if (count == 0) {
            display_draw_text(2, 0, "No codes saved");
            display_draw_text(3, 0, "Use IR Learn first");
        } else {
            for (int i = 0; i < count && i < DISPLAY_ROWS - 2; i++) {
                const ir_library_entry_t *e = ir_library_get(i);
                char line[DISPLAY_COLS + 1];
                snprintf(line, sizeof(line), "%c%.19s", (i == selected) ? '>' : ' ', e->name);
                display_draw_text(1 + i, 0, line);
            }
        }
        display_draw_text(7, 0, count > 0 ? "PRESS:send LEFT:del" : "BACK: exit");
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
            ir_library_remove(selected);
            ir_library_save(); // best-effort, see action_ir_learn()'s comment
            if (selected >= ir_library_count() && selected > 0) {
                selected--;
            }
        }
    }

    menu_render(s_active_menu);
}

// Opens the 4-receiver direction-finding screen. ir_direction_init() is not
// called from app_main() on this build (see KNOWN_ISSUES.md's Round 13 --
// the P4 has no spare RMT RX channel once the regular IR receiver/
// transmitter are running, and GPIO14/15 are now also claimed by the LCD's
// SPI wiring), so ir_direction_poll() always reports "no frame" here --
// render_ir_direction_screen() shows that as an explicit "not available"
// message rather than an infinite unexplained "Waiting for IR...".
static void action_ir_direction_find(void)
{
    s_screen = APP_SCREEN_IR_DIRECTION;
    s_last_scan_line[0] = '\0';
    s_screen_dirty = true;
}

// Blocks for a few seconds while the C6 scans. Replace with a real
// "browse networks" screen once there's a UI for picking one to connect to.
static void action_wifi_scan_test(void)
{
    c6_network_t networks[C6_MAX_NETWORKS];
    int count = c6_link_scan(networks, C6_MAX_NETWORKS);
    if (count < 0) {
        ESP_LOGW(TAG, "Wi-Fi scan failed (C6 not responding?)");
        diag_record_error("WiFi Scan Test", "C6_LINK_SCAN_FAILED");
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi scan found %d network(s):", count);
    for (int i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  %s (%d dBm)", networks[i].ssid, networks[i].rssi);
    }
}

// Blocks (up to several minutes) while the C6 runs its web setup AP.
// Shows instructions on the OLED since there's nothing else to do here --
// the actual ssid/password entry happens on the user's phone browser.
// Fills `out_pin` (capacity out_cap, must be > C6_SETUP_PIN_LEN) with a
// fresh random WPA2-PSK password for this setup session, using the P4's
// hardware RNG (esp_random() -- true entropy, not a PRNG seeded from
// something guessable). Alphanumeric, uppercase+digits only (no lowercase)
// so it's unambiguous to read off the small OLED and type on a phone
// keyboard -- this trades a little entropy for usability, still far more
// than the single fixed password this replaced.
static void generate_setup_pin(char *out_pin, size_t out_cap)
{
    static const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no O/0/I/1 (ambiguous on-screen)
    size_t len = out_cap - 1;
    for (size_t i = 0; i < len; i++) {
        out_pin[i] = charset[esp_random() % (sizeof(charset) - 1)];
    }
    out_pin[len] = '\0';
}

static void action_wifi_setup(void)
{
    char pin[C6_SETUP_PIN_LEN + 1];
    generate_setup_pin(pin, sizeof(pin));

    display_clear();
    display_draw_text_color(0, 0, "WiFi Setup", DISPLAY_COLOR_ACCENT);
    display_draw_text(1, 0, "AP: MakeshiftFlip");
    display_draw_text(2, 0, "per-Setup");
    char pwd_line[DISPLAY_COLS + 1];
    snprintf(pwd_line, sizeof(pwd_line), "Pwd: %s", pin);
    display_draw_text(3, 0, pwd_line);
    display_draw_text(5, 0, "Then open 192.168");
    display_draw_text(6, 0, ".4.1 in browser");
    display_flush();

    bool ok = c6_link_setup(pin);
    if (!ok) {
        diag_record_error("WiFi Setup", "C6_LINK_SETUP_FAILED");
    }

    display_clear();
    display_draw_text_color(0, 0, "WiFi Setup", DISPLAY_COLOR_ACCENT);
    display_draw_text_color(2, 0, ok ? "Connected!" : "Failed / timed out",
                             ok ? DISPLAY_COLOR_OK : DISPLAY_COLOR_ERROR);
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
    display_clear();
    display_draw_text_color(0, 0, "Scanning...", DISPLAY_COLOR_ACCENT);
    display_flush();

    c6_network_t networks[C6_MAX_NETWORKS];
    int count = c6_link_scan(networks, C6_MAX_NETWORKS);
    if (count <= 0) {
        display_clear();
        display_draw_text_color(0, 0, "No networks found", DISPLAY_COLOR_ERROR);
        display_draw_text(2, 0, "Press any key");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }

    // --- Network picker: UP/DOWN moves, PRESS selects, LEFT cancels. ---
    int selected = 0;
    bool picked = false;
    bool cancelled = false;
    for (;;) {
        display_clear();
        display_draw_text_color(0, 0, "Pick a network:", DISPLAY_COLOR_ACCENT);
        for (int i = 0; i < count && i < DISPLAY_ROWS - 1; i++) {
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "%c%.20s", (i == selected) ? '>' : ' ', networks[i].ssid);
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
    display_clear();
    display_draw_text_color(0, 0, "WiFi Monitor", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Starting...");
    display_flush();

    if (!c6_link_monitor_start()) {
        diag_record_error("WiFi Monitor", "C6_LINK_MONITOR_START_FAILED");
        display_clear();
        display_draw_text_color(0, 0, "WiFi Monitor", DISPLAY_COLOR_ACCENT);
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
        snprintf(header, sizeof(header), "WiFi Monitor (%d)", count);
        display_draw_text_color(0, 0, header, DISPLAY_COLOR_ACCENT);
        for (int i = 0; i < count && i < DISPLAY_ROWS - 2; i++) {
            char line[DISPLAY_COLS + 1];
            // Clamp both text fields so the largest channel/RSSI values
            // plus the terminator always fit within DISPLAY_COLS.
            snprintf(line, sizeof(line), "%.6s %.4s c%u %d",
                     aps[i].ssid[0] ? aps[i].ssid : "(hid)",
                     aps[i].sec[0] ? aps[i].sec : "?",
                     aps[i].channel, aps[i].rssi);
            display_draw_text(1 + i, 0, line);
        }
        display_draw_text(7, 0, "BACK: stop+exit");
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
// (see c6_link.h's c6_link_bt_scan_start() comment for why it shares the
// same "blocks every other C6 feature while open" behavior, even though
// it's a different radio -- the UART link is the actual bottleneck).
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
        for (int i = 0; i < count && i < DISPLAY_ROWS - 2; i++) {
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "%.13s %ddBm",
                     devices[i].name[0] ? devices[i].name : "(no name)", devices[i].rssi);
            display_draw_text(1 + i, 0, line);
        }
        display_draw_text(7, 0, "BACK: stop+exit");
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
    display_draw_text(3, 0, "ESP32-P4 + ESP32-C6");
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

// Uploads the on-screen error history to the PC-side log server (see
// c6_link.h's c6_link_send_error_log() comment) and shows the result.
// Blocking, same "do the thing, show a result screen, wait for any key"
// shape as the other network-backed actions in this file. Entirely
// optional -- the history itself already works without this ever being
// called.
static void action_send_error_log(const diag_entry_t *entries, int count)
{
    display_clear();
    display_draw_text_color(0, 0, "Errors", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Sending...");
    display_flush();

    bool ok = c6_link_send_error_log(entries, count);

    display_clear();
    display_draw_text_color(0, 0, "Errors", DISPLAY_COLOR_ACCENT);
    display_draw_text_color(2, 0, ok ? "Sent!" : "Send failed",
                             ok ? DISPLAY_COLOR_OK : DISPLAY_COLOR_ERROR);
    if (!ok) {
        display_draw_text(3, 0, "Check WiFi / log");
        display_draw_text(4, 0, "server config");
    }
    display_draw_text(6, 0, "Press any key");
    display_flush();

    wait_for_any_key();
}

// Shows the device's local error history (main/diag/diag.h), newest
// first: module/code/relative-age per line, UP/DOWN to scroll one entry
// at a time, PRESS to upload the whole list via action_send_error_log()
// (optional -- needs Wi-Fi and a running debug_server.py, see
// c6-firmware/README.md), BACK to exit. A one-time snapshot taken when the
// screen opens, not live-polled -- errors recorded while this screen is
// open won't appear until it's reopened.
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
            for (int i = 0; i < count - top && i < DISPLAY_ROWS - 2; i++) {
                const diag_entry_t *e = &entries[top + i];
                char ago[16];
                format_relative_time(ago, sizeof(ago), e->timestamp_us);
                char line[DISPLAY_COLS + 1];
                snprintf(line, sizeof(line), "%.12s %.9s %.7s", e->module, e->code, ago);
                display_draw_text(1 + i, 0, line);
            }
        }
        display_draw_text(7, 0, count > 0 ? "PRESS:send BACK:exit" : "BACK: exit");
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK) {
            break;
        }
        if (event == BUTTON_PRESS && count > 0) {
            action_send_error_log(entries, count);
        } else if (event == BUTTON_UP && top > 0) {
            top--;
        } else if (event == BUTTON_DOWN && top < count - 1) {
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
    {"IR Direction Find", action_ir_direction_find, NULL},
};

static menu_item_t s_wifi_menu_items[] = {
    {"WiFi Scan Test",     action_wifi_scan_test,   NULL},
    {"WiFi Setup",          action_wifi_setup,       NULL},
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
    // The P4 has no spare RMT RX channels after the regular IR receiver and
    // transmitter are enabled. The four-receiver direction finder is kept
    // available in code for a future hardware profile, but must not consume
    // channels on this base build.
    rc522_init();
    rdm6300_init();
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
            bool found = false;

            if (s_screen == APP_SCREEN_SCAN_125KHZ) {
                rdm6300_id_t id;
                if (rdm6300_poll(&id)) {
                    snprintf(s_last_scan_line, sizeof(s_last_scan_line),
                             "%02X%02X%02X%02X%02X",
                             id.bytes[0], id.bytes[1], id.bytes[2], id.bytes[3], id.bytes[4]);
                    found = true;
                }
            } else {
                rc522_uid_t uid;
                rc522_scan_result_t result = rc522_read_uid(&uid);
                if (result == RC522_SCAN_OK) {
                    int n = 0;
                    for (int i = 0; i < uid.length && n < DISPLAY_COLS - 3; i++) {
                        n += snprintf(&s_last_scan_line[n], sizeof(s_last_scan_line) - n,
                                      "%02X ", uid.bytes[i]);
                    }
                    found = true;
                } else if (result == RC522_SCAN_UNSUPPORTED_UID) {
                    snprintf(s_last_scan_line, sizeof(s_last_scan_line),
                             "7/10-byte UID: N/A");
                    found = true;
                    diag_record_error("13.56MHz NFC", "RC522_SCAN_UNSUPPORTED_UID");
                } else if (result == RC522_SCAN_ERROR) {
                    diag_record_error("13.56MHz NFC", "RC522_SCAN_ERROR");
                }
            }

            if (found) {
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
