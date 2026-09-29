#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "hardware_profile.h"
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
#include "feedback/battery.h"
#include "ui/display.h"
#include "ui/menu.h"
#include "ui/text_entry.h"
#include "ui/qrcode.h"

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

// Wait up to `ticks` * 10ms for one joystick edge. Live radio lists keep
// refreshing even without input; detail screens pass a short timeout and
// repeat until LEFT/BACK is pressed.
static button_id_t poll_button_for_ticks(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        button_id_t event = buttons_poll();
        if (event != BUTTON_COUNT) {
            return event;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return BUTTON_COUNT;
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
        display_draw_text_centered(0, title, DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, prompt_line);
        display_draw_text(6, 0, "GERİ: iptal");
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
                display_draw_text_centered(0, title, DISPLAY_COLOR_ACCENT);
                display_draw_text_color(2, 0, "7/10 bayt UID", DISPLAY_COLOR_ERROR);
                display_draw_text_color(3, 0, "desteklenmiyor", DISPLAY_COLOR_ERROR);
                display_draw_text(6, 0, "Bir tuşa bas");
                display_flush();
                diag_record_error(diag_module, "RC522_SCAN_UNSUPPORTED_UID");
                wait_for_any_key();
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// Brief startup notice. This is a usage reminder, not a claim that a
// disclaimer removes legal responsibility. It times out so an unwired or
// broken joystick cannot prevent the firmware from booting.
// Boot splash: a short intro animation before the legal notice. Concentric
// rings expand outward from the centre (like a radar ping) while the ERDEMFLIP
// name fades in, then the whole thing holds briefly. Pure drawing primitives,
// no input; a key press skips straight to the notice.
static void show_boot_splash(void)
{
    const int cx = DISPLAY_WIDTH_PX / 2;
    const int cy = DISPLAY_HEIGHT_PX / 2;
    display_set_background(DISPLAY_BG_DEFAULT);

    // 24 frames of expanding rings. Three rings chase each other outward.
    for (int f = 0; f < 24; f++) {
        display_clear();
        for (int k = 0; k < 3; k++) {
            int r = ((f + k * 8) % 24) * 5; // 0..115, staggered
            if (r > 4) {
                // Fade: outer rings dimmer than inner ones.
                display_color_t col = (r < 40) ? DISPLAY_COLOR_ACCENT
                                    : (r < 80) ? DISPLAY_RGB(20, 10, 2)
                                               : DISPLAY_COLOR_DIM;
                display_draw_circle(cx, cy, r, col);
            }
        }
        display_fill_circle(cx, cy, 4, DISPLAY_COLOR_ACCENT);
        // Name appears after the first few frames.
        if (f > 5) {
            display_draw_text_centered(6, "ERDEMFLIP", DISPLAY_COLOR_TEXT);
        }
        if (f > 10) {
            display_draw_text_centered(8, "Makeshift Flipper", DISPLAY_COLOR_DIM);
        }
        display_flush();

        // A key press skips the rest of the splash.
        if (buttons_poll() != BUTTON_COUNT) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    vTaskDelay(pdMS_TO_TICKS(400)); // brief hold on the finished logo
}

static void show_startup_notice(void)
{
    display_set_background(DISPLAY_BG_DEFAULT);
    display_clear();
    display_draw_text_centered(1, "YASAL UYARI", DISPLAY_COLOR_ERROR);
    display_draw_text(4, 1, "Yalnızca kendi veya açıkça");
    display_draw_text(5, 1, "izinli cihazlarda kullan.");
    display_draw_text(7, 1, "Kullanımdan doğan hukuki");
    display_draw_text(8, 1, "sorumluluk kullanıcıya aittir.");
    display_draw_text(10, 1, "Bu uyarı izin yerine geçmez.");
    display_draw_text_centered(13, "SAĞ: devam (5 sn)", DISPLAY_COLOR_ACCENT);
    display_flush();
    for (int i = 0; i < 500; i++) {
        button_id_t event = buttons_poll();
        if (event == BUTTON_RIGHT || event == BUTTON_PRESS) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Don't let the RIGHT/PRESS that dismissed this notice leak into the first
    // menu render as a fresh "enter", which was launching the RFID submenu at
    // boot. Wait for the key to be released and clear the button edge state.
    buttons_wait_all_released();
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
        char header[DISPLAY_COLS * 2 + 1];
        snprintf(header, sizeof(header), "Sektör %d/%d %s", sector, RC522_SECTOR_COUNT - 1,
                  dump->sectors[sector].readable ? "" : "(kilitli)");
        display_draw_text_centered(0, header, DISPLAY_COLOR_ACCENT);

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
            display_draw_text(2, 0, "Varsayılan anahtar yok");
        }

        char footer[DISPLAY_COLS * 2 + 1];
        snprintf(footer, sizeof(footer), "%d/%d sektör okundu", dump->sectors_read, RC522_SECTOR_COUNT);
        display_draw_text(DISPLAY_ROWS - 2, 0, footer);
        display_draw_text(DISPLAY_ROWS - 1, 0, "BAS:kopyala GERİ:çık");
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

    if (!wait_for_card("RFID Kopyala", "Kaynak kartı okut", "RFID Clone", &dump->uid)) {
        free(dump);
        rc522_antenna_off();
        menu_render(s_active_menu);
        return;
    }

    display_clear();
    display_draw_text_centered(0, "RFID Kopyala", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Sektörler okunuyor...");
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
    if (!wait_for_card("RFID Kopyala", "Hedef kartı okut", "RFID Clone", &target_uid)) {
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
    display_draw_text_centered(0, "RFID Kopyala", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Sektörler yazılıyor...");
    display_flush();
    int written = clone_to_card(&target_uid, dump, /* write_trailers = */ false);
    if (written == 0) {
        diag_record_error("RFID Clone", "RC522_CLONE_WRITE_FAILED");
    }

    display_clear();
    display_draw_text_centered(0, "RFID Kopyala", DISPLAY_COLOR_ACCENT);
    char result_line[DISPLAY_COLS * 2 + 1];
    snprintf(result_line, sizeof(result_line), "%d/%d sektör kopyalandı", written, dump->sectors_read);
    display_draw_text(2, 0, result_line);
    display_draw_text(3, 0, !uid_source_ok ? "UID okunmadı; atlandı"
                            : is_magic      ? "UID kopyalandı (gen1a)"
                                            : "UID kopyalanmadı");
    display_draw_text(5, 0, "Anahtar blokları yazılmadı");
    display_draw_text(6, 0, "(yalnızca veri blokları)");
    display_draw_text(7, 0, "Bir tuşa bas");
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
    buttons_keyboard_reset();
    for (;;) {
        text_entry_render(entry, prompt, /* mask = */ false);

        button_id_t event;
        do {
            event = buttons_poll_keyboard();
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
        display_draw_text_centered(0, "125kHz Etiket Kaydet", DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, "Etiketi okut");
        display_draw_text(6, 0, "GERİ: iptal");
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
    bool cancelled = !prompt_for_name(&entry, "Etikete ad ver");

    display_clear();
    display_draw_text_centered(0, "125kHz Etiket Kaydet", DISPLAY_COLOR_ACCENT);
    if (cancelled) {
        display_draw_text_color(2, 0, "İptal edildi", DISPLAY_COLOR_DIM);
    } else if (entry.length == 0) {
        display_draw_text_color(2, 0, "Ad boş olamaz", DISPLAY_COLOR_ERROR);
    } else if (!rfid_library_add_125khz(entry.buffer, &id)) {
        display_draw_text_color(2, 0, "Kütüphane dolu", DISPLAY_COLOR_ERROR);
        display_draw_text(3, 0, "Önce bir kayıt sil");
        diag_record_error("RFID Save", "RFID_LIBRARY_FULL");
    } else {
        rfid_library_save(); // best-effort, same as ir_library_save()
        display_draw_text_color(2, 0, "Kaydedildi!", DISPLAY_COLOR_OK);
    }
    display_draw_text(6, 0, "Bir tuşa bas");
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
    if (!wait_for_card("13.56MHz Etiket Kaydet", "Etiketi okut", "RFID Save", &uid)) {
        rc522_antenna_off();
        menu_render(s_active_menu);
        return;
    }
    rc522_antenna_off();

    vibration_pulse(80);

    text_entry_t entry;
    bool cancelled = !prompt_for_name(&entry, "Etikete ad ver");

    display_clear();
    display_draw_text_centered(0, "13.56MHz Etiket Kaydet", DISPLAY_COLOR_ACCENT);
    if (cancelled) {
        display_draw_text_color(2, 0, "İptal edildi", DISPLAY_COLOR_DIM);
    } else if (entry.length == 0) {
        display_draw_text_color(2, 0, "Ad boş olamaz", DISPLAY_COLOR_ERROR);
    } else if (!rfid_library_add_1356mhz(entry.buffer, &uid)) {
        display_draw_text_color(2, 0, "Kütüphane dolu", DISPLAY_COLOR_ERROR);
        display_draw_text(3, 0, "Önce bir kayıt sil");
        diag_record_error("RFID Save", "RFID_LIBRARY_FULL");
    } else {
        rfid_library_save(); // best-effort, same as ir_library_save()
        display_draw_text_color(2, 0, "Kaydedildi!", DISPLAY_COLOR_OK);
    }
    display_draw_text(6, 0, "Bir tuşa bas");
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
        display_draw_text_centered(0, "Silinsin mi?", DISPLAY_COLOR_ERROR);
        display_draw_text(2, 0, item_name);
        display_draw_text_color(5, 0, on_delete ? "  İptal" : "> İptal",
                                on_delete ? DISPLAY_COLOR_TEXT : DISPLAY_COLOR_ACCENT);
        display_draw_text_color(6, 0, on_delete ? "> Sil" : "  Sil",
                                on_delete ? DISPLAY_COLOR_ERROR : DISPLAY_COLOR_TEXT);
        display_draw_text(DISPLAY_ROWS - 1, 0, "YUK/AŞA SAĞ:onay SOL:geri");
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            return false;
        } else if (event == BUTTON_UP || event == BUTTON_DOWN) {
            on_delete = !on_delete;
        } else if (event == BUTTON_PRESS || event == BUTTON_RIGHT) {
            return on_delete;
        }
    }
}

static bool show_rfid_entry(const rfid_library_entry_t *entry)
{
    bool delete_selected = false;
    for (;;) {
        display_clear();
        display_draw_text_centered(0, "RFID Kaydı", DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, entry->name);
        display_draw_text(3, 0, entry->kind == RFID_LIBRARY_KIND_125KHZ
                                ? "125kHz UID" : "13.56MHz UID");
        const uint8_t *bytes = entry->kind == RFID_LIBRARY_KIND_125KHZ
                             ? entry->u.id_125khz.bytes : entry->u.uid_1356mhz.bytes;
        int length = entry->kind == RFID_LIBRARY_KIND_125KHZ
                   ? 5 : entry->u.uid_1356mhz.length;
        char uid_line[DISPLAY_COLS * 2 + 1];
        int n = snprintf(uid_line, sizeof(uid_line), "UID: ");
        for (int i = 0; i < length && n < (int)sizeof(uid_line) - 3; i++) {
            n += snprintf(uid_line + n, sizeof(uid_line) - n, "%02X", bytes[i]);
        }
        display_draw_text(5, 0, uid_line);
        display_draw_text_color(9, 0, delete_selected ? "  Geri" : "> Geri",
                                delete_selected ? DISPLAY_COLOR_TEXT : DISPLAY_COLOR_ACCENT);
        display_draw_text_color(10, 0, delete_selected ? "> Sil" : "  Sil",
                                delete_selected ? DISPLAY_COLOR_ERROR : DISPLAY_COLOR_TEXT);
        display_draw_text(14, 0, "YUK/AŞA SAĞ:seç SOL:geri");
        display_flush();

        button_id_t event = poll_button_for_ticks(50);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            return false;
        }
        if (event == BUTTON_UP || event == BUTTON_DOWN) {
            delete_selected = !delete_selected;
        } else if (event == BUTTON_RIGHT || event == BUTTON_PRESS) {
            return delete_selected && confirm_delete(entry->name);
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
        char header[DISPLAY_COLS * 2 + 1];
        snprintf(header, sizeof(header), "RFID Kayıtları (%d)", count);
        display_draw_text_centered(0, header, DISPLAY_COLOR_ACCENT);

        if (count == 0) {
            display_draw_text(2, 0, "Kayıtlı etiket yok");
            display_draw_text(3, 0, "Önce etiket kaydet");
        } else {
            for (int i = 0; i < count - top && i < LIST_VISIBLE_ROWS; i++) {
                const rfid_library_entry_t *e = rfid_library_get(top + i);
                char line[DISPLAY_COLS + 1];
                char kind = (e->kind == RFID_LIBRARY_KIND_125KHZ) ? 'L' : 'H'; // Low/High freq
                snprintf(line, sizeof(line), "%c%c %.17s", (top + i == selected) ? '>' : ' ', kind, e->name);
                display_draw_text(LIST_HEADER_ROWS + i, 0, line);
            }
        }
        display_draw_text(DISPLAY_ROWS - 1, 0,
                          count > 0 ? "YUK/AŞA SAĞ:bilgi SOL:çık" : "SOL: çık");
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            break;
        } else if (event == BUTTON_UP && selected > 0) {
            selected--;
        } else if (event == BUTTON_DOWN && selected < count - 1) {
            selected++;
        } else if ((event == BUTTON_RIGHT || event == BUTTON_PRESS) && count > 0) {
            const rfid_library_entry_t *e = rfid_library_get(selected);
            if (show_rfid_entry(e)) {
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
    display_draw_text_centered(0, "IR Gönderim Testi", DISPLAY_COLOR_ACCENT);
    display_draw_text_color(2, 0, "Test çerçevesi gönderildi", DISPLAY_COLOR_OK);
    display_draw_text(3, 0, "addr=0x00 cmd=0x45");
    display_draw_text(6, 0, "Bir tuşa bas");
    display_flush();
    wait_for_any_key();
}

// Blocks (BACK cancels) waiting for one NEC frame via ir_driver_poll_rx(),
// then lets the user name it with the scroll keyboard and saves it to the
// library. Its naming step uses the same scroll keyboard as Wi-Fi setup.
static void action_ir_learn(void)
{
    ir_nec_frame_t frame;
    bool captured = false;
    for (;;) {
        display_clear();
        display_draw_text_centered(0, "IR Öğren", DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, "Kumandayı buraya tut");
        display_draw_text(3, 0, "ve bir düğmeye bas");
        display_draw_text(6, 0, "GERİ: iptal");
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
    bool cancelled = !prompt_for_name(&entry, "Koda ad ver");

    display_clear();
    display_draw_text_centered(0, "IR Öğren", DISPLAY_COLOR_ACCENT);
    if (cancelled) {
        display_draw_text_color(2, 0, "İptal edildi", DISPLAY_COLOR_DIM);
    } else if (entry.length == 0) {
        display_draw_text_color(2, 0, "Ad boş olamaz", DISPLAY_COLOR_ERROR);
    } else if (!ir_library_add(entry.buffer, &frame)) {
        display_draw_text_color(2, 0, "Kütüphane dolu", DISPLAY_COLOR_ERROR);
        display_draw_text(3, 0, "Önce bir kayıt sil");
        diag_record_error("IR Learn", "IR_LIBRARY_FULL");
    } else {
        ir_library_save(); // best-effort, same as diag_save() -- RAM copy is authoritative regardless
        display_draw_text_color(2, 0, "Kaydedildi!", DISPLAY_COLOR_OK);
    }
    display_draw_text(6, 0, "Bir tuşa bas");
    display_flush();

    wait_for_any_key();
    menu_render(s_active_menu);
}

// Right opens one saved code, where UP/DOWN choose send/delete and LEFT
// backs out. The physical joystick has no centre press.
static bool show_ir_entry(const ir_library_entry_t *entry)
{
    bool delete_selected = false;
    for (;;) {
        display_clear();
        display_draw_text_centered(0, "IR Kaydı", DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, entry->name);
        char line[DISPLAY_COLS + 1];
        snprintf(line, sizeof(line), "Adres: 0x%02X Kod: 0x%02X",
                 entry->frame.address, entry->frame.command);
        display_draw_text(4, 0, line);
        display_draw_text_color(9, 0, delete_selected ? "  Gönder" : "> Gönder",
                                delete_selected ? DISPLAY_COLOR_TEXT : DISPLAY_COLOR_ACCENT);
        display_draw_text_color(10, 0, delete_selected ? "> Sil" : "  Sil",
                                delete_selected ? DISPLAY_COLOR_ERROR : DISPLAY_COLOR_TEXT);
        display_draw_text(14, 0, "YUK/AŞA SAĞ:seç SOL:geri");
        display_flush();

        button_id_t event = poll_button_for_ticks(50);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            return false;
        }
        if (event == BUTTON_UP || event == BUTTON_DOWN) {
            delete_selected = !delete_selected;
        } else if (event == BUTTON_RIGHT || event == BUTTON_PRESS) {
            if (delete_selected) {
                return confirm_delete(entry->name);
            }
            ir_driver_send(&entry->frame);
            vibration_pulse(80);
            return false;
        }
    }
}

// Browses saved IR codes, oldest first. The selected entry's action menu
// provides both send and delete while LEFT always means exit.
static void action_ir_library(void)
{
    int selected = 0;
    int top = 0;
    for (;;) {
        int count = ir_library_count();
        list_clamp_scroll(selected, &top);
        display_clear();
        char header[DISPLAY_COLS * 2 + 1];
        snprintf(header, sizeof(header), "IR Kayıtları (%d)", count);
        display_draw_text_centered(0, header, DISPLAY_COLOR_ACCENT);

        if (count == 0) {
            display_draw_text(2, 0, "Kayıtlı kod yok");
            display_draw_text(3, 0, "Önce IR kodu öğren");
        } else {
            for (int i = 0; i < count - top && i < LIST_VISIBLE_ROWS; i++) {
                const ir_library_entry_t *e = ir_library_get(top + i);
                char line[DISPLAY_COLS + 1];
                snprintf(line, sizeof(line), "%c%.19s", (top + i == selected) ? '>' : ' ', e->name);
                display_draw_text(LIST_HEADER_ROWS + i, 0, line);
            }
        }
        display_draw_text(DISPLAY_ROWS - 1, 0,
                          count > 0 ? "YUK/AŞA SAĞ:bilgi SOL:çık" : "SOL: çık");
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);

        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            break;
        } else if (event == BUTTON_UP && selected > 0) {
            selected--;
        } else if (event == BUTTON_DOWN && selected < count - 1) {
            selected++;
        } else if ((event == BUTTON_RIGHT || event == BUTTON_PRESS) && count > 0) {
            const ir_library_entry_t *e = ir_library_get(selected);
            if (show_ir_entry(e)) {
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

// Station-mode scan/pick/connect. The local AP has its own menu action;
// passive Wi-Fi monitoring lives separately under Hacking.
static void action_wifi_scan_test(void)
{
    if (c6_link_ap_is_running()) {
        display_clear();
        display_draw_text_centered(0, "WiFi Tarama [STA]", DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, "Önce WiFi Ağım'ı kapat");
        display_draw_text(6, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }
    // "[STA]" marks this as a station-mode scan (normal client behavior,
    // same as a phone listing nearby Wi-Fi) so it's visually distinct from
    // "[MON]" (promiscuous Wi-Fi Monitor, below) -- the two use different
    // radio modes and the difference matters (Monitor drops any STA
    // connection; a station scan doesn't).
    display_clear();
    display_draw_text_centered(0, "WiFi Tarama [STA]", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Taranıyor...");
    display_flush();

    // static: ~0.5KB scan-result array kept off the small main task stack, same
    // reasoning as the BLE/QR buffers. Called only from the single-threaded UI.
    static c6_network_t networks[C6_MAX_NETWORKS];
    int count = c6_link_scan(networks, C6_MAX_NETWORKS);

    if (count < 0) {
        ESP_LOGW(TAG, "Wi-Fi scan failed (C6 not responding?)");
        diag_record_error("WiFi Scan Test", "C6_LINK_SCAN_FAILED");
        display_clear();
        display_draw_text_centered(0, "WiFi Tarama [STA]", DISPLAY_COLOR_ACCENT);
        display_draw_text_color(2, 0, "Tarama başarısız", DISPLAY_COLOR_ERROR);
        display_draw_text(3, 0, "C6 yanıt vermiyor mu?");
        display_draw_text(DISPLAY_ROWS - 1, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }
    if (count == 0) {
        display_clear();
        display_draw_text_centered(0, "WiFi Tarama [STA]", DISPLAY_COLOR_ACCENT);
        display_draw_text_color(2, 0, "Ağ bulunamadı", DISPLAY_COLOR_DIM);
        display_draw_text(DISPLAY_ROWS - 1, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }

    // Sort nearest-first by RSSI (higher = stronger = closer) so the closest
    // networks are at the top. One-shot scan, so sorting once here keeps the
    // selection index consistent for the whole screen. Insertion sort; small n.
    for (int a = 1; a < count; a++) {
        c6_network_t key = networks[a];
        int b = a - 1;
        while (b >= 0 && networks[b].rssi < key.rssi) {
            networks[b + 1] = networks[b];
            b--;
        }
        networks[b + 1] = key;
    }

    // --- Scrollable network list: UP/DOWN moves, RIGHT/PRESS connects,
    // LEFT/BACK leaves. Shows signal strength (dBm: closer to 0 = stronger),
    // sorted nearest-first. Window scrolls with `top`. ---
    int selected = 0;
    int top = 0;
    bool picked = false;
    for (;;) {
        list_clamp_scroll(selected, &top);
        display_clear();
        char header[DISPLAY_COLS * 2 + 1];
        snprintf(header, sizeof(header), "WiFi: %d ağ bulundu", count);
        display_draw_text_centered(0, header, DISPLAY_COLOR_ACCENT);
        for (int i = 0; i < count - top && i < LIST_VISIBLE_ROWS; i++) {
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "%c%.13s %ddBm",
                     (top + i == selected) ? '>' : ' ',
                     networks[top + i].ssid, networks[top + i].rssi);
            display_draw_text(1 + i, 0, line);
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, "YUK/AŞA SAĞ:bağlan SOL:çık");
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
        } else if (event == BUTTON_PRESS || event == BUTTON_RIGHT) {
            picked = true;
            break;
        } else if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            break;
        }
    }

    if (!picked) {
        menu_render(s_active_menu);
        return;
    }

    // --- Password entry via the scroll keyboard. ---
    text_entry_t entry;
    text_entry_init(&entry);
    buttons_keyboard_reset();
    char title[DISPLAY_COLS * 2 + 1];
    snprintf(title, sizeof(title), "Şifre: %.12s", networks[selected].ssid);

    bool entry_cancelled = false;
    for (;;) {
        text_entry_render(&entry, title, /* mask = */ true);

        button_id_t event;
        do {
            event = buttons_poll_keyboard();
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
    display_draw_text_centered(0, "Bağlanıyor...", DISPLAY_COLOR_ACCENT);
    display_flush();

    bool ok = c6_link_connect(networks[selected].ssid, entry.buffer);
    if (!ok) {
        diag_record_error("WiFi Scan Test", "C6_LINK_CONNECT_FAILED");
    }

    display_clear();
    display_draw_text_centered(0, "WiFi Tarama [STA]", DISPLAY_COLOR_ACCENT);
    display_draw_text_color(2, 0, ok ? "Bağlandı!" : "Başarısız",
                             ok ? DISPLAY_COLOR_OK : DISPLAY_COLOR_ERROR);
    display_draw_text(6, 0, "Bir tuşa bas");
    display_flush();
    wait_for_any_key();
    menu_render(s_active_menu);
}

// Local station diagnostics. Association and DHCP do not by themselves
// prove Internet access, and the router's client list is not available to
// an ordinary Wi-Fi station without a router-specific administration API.
static void action_wifi_status(void)
{
    for (;;) {
        c6_wifi_status_t status;
        bool connected = c6_link_get_wifi_status(&status);
        display_clear();
        display_draw_text_centered(0, "WiFi Durum", DISPLAY_COLOR_ACCENT);
        if (connected) {
            char line[DISPLAY_COLS * 2 + 1];
            snprintf(line, sizeof(line), "Ağ: %.23s", status.ssid);
            display_draw_text(2, 0, line);
            snprintf(line, sizeof(line), "Sinyal: %d dBm", status.rssi);
            display_draw_text(3, 0, line);
            snprintf(line, sizeof(line), "Kanal: %u", status.channel);
            display_draw_text(4, 0, line);
            snprintf(line, sizeof(line), "IP: %s", status.ip);
            display_draw_text(6, 0, line);
            snprintf(line, sizeof(line), "Ağ geçidi: %s", status.gateway);
            display_draw_text(7, 0, line);
            display_draw_text(9, 0, "Ağdaki cihaz sayısı: bilinmez");
            display_draw_text(11, 0, "İnternet doğrulanmadı");
        } else if (c6_link_ap_is_running()) {
            display_draw_text_color(2, 0, "Yerel WiFi ağı açık", DISPLAY_COLOR_OK);
            display_draw_text(4, 0, "Bilgi: WiFi Ağım menüsü");
        } else {
            display_draw_text_color(2, 0, "WiFi bağlı değil", DISPLAY_COLOR_DIM);
            display_draw_text(4, 0, "Önce Tara/Bağlan kullan");
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, "SAĞ:yenile SOL:çık");
        display_flush();

        button_id_t event;
        do {
            event = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (event == BUTTON_COUNT);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            break;
        }
    }
    menu_render(s_active_menu);
}

// A local WPA2 SoftAP with DHCP, but no Internet uplink or application
// service. The AP stays up after leaving this screen until explicitly
// stopped here; the password changes on each new start and is shown only
// on the device. START/STOP deliberately changes the radio's STA/AP mode.
// Draws a QR code centered on a white quiet-zone background, scaled to fill
// the panel, and waits for a key. QR readers need a light background with dark
// modules and a margin, so we invert the panel to white here regardless of the
// current theme tint.
static void show_qr_screen(const qr_code_t *qr, const char *caption)
{
    // White background (framebuffer expects DISPLAY_RGB values via helpers;
    // fill the whole panel white, then draw dark modules).
    display_fill_rect(0, 0, DISPLAY_WIDTH_PX, DISPLAY_HEIGHT_PX, DISPLAY_RGB(31, 63, 31));

    int quiet = 4; // modules of margin
    int total = qr->size + quiet * 2;
    int scale = DISPLAY_WIDTH_PX / total;
    if (scale < 1) {
        scale = 1;
    }
    int drawn = qr->size * scale;
    int origin = (DISPLAY_WIDTH_PX - drawn) / 2;
    for (int y = 0; y < qr->size; y++) {
        for (int x = 0; x < qr->size; x++) {
            if (qr->modules[y * qr->size + x]) {
                display_fill_rect(origin + x * scale, origin + y * scale,
                                  scale, scale, DISPLAY_RGB(0, 0, 0));
            }
        }
    }
    if (caption) {
        // one-line caption in the bottom margin, dark on the white field
        display_draw_text_px(4, DISPLAY_HEIGHT_PX - 16, caption,
                             DISPLAY_RGB(0, 0, 0), DISPLAY_RGB(31, 63, 31));
    }
    display_flush();
    wait_for_any_key();
}

static void action_wifi_my_network(void)
{
    bool operation_failed = false;
    for (;;) {
        c6_ap_status_t status;
        bool active = c6_link_ap_get_status(&status);
        display_clear();
        display_draw_text_centered(0, "WiFi Ağım [AP]", DISPLAY_COLOR_ACCENT);
        if (active) {
            char line[DISPLAY_COLS + 1];
            display_draw_text_color(2, 0, "Ağ açık - WPA2", DISPLAY_COLOR_OK);
            snprintf(line, sizeof(line), "Ad: %.25s", status.ssid);
            display_draw_text(4, 0, line);
            snprintf(line, sizeof(line), "Şifre: %s", status.password);
            display_draw_text(5, 0, line);
            snprintf(line, sizeof(line), "Yerel IP: %s", status.ip);
            display_draw_text(7, 0, line);
            if (status.client_count >= 0) {
                snprintf(line, sizeof(line), "Bağlı cihaz: %d/4", status.client_count);
            } else {
                snprintf(line, sizeof(line), "Bağlı cihaz: ?/4");
            }
            display_draw_text(8, 0, line);
            display_draw_text(10, 0, "İnternet / web servisi yok");
            display_draw_text(11, 0, "YUKARI: QR ile paylaş");
            display_draw_text(12, 0, "SOL: çık (ağ açık kalır)");
            display_draw_text(14, 0, "SAĞ/A: ağı kapat");
        } else {
            display_draw_text_color(2, 0, "Ağ kapalı", DISPLAY_COLOR_DIM);
            display_draw_text(4, 0, "SAĞ/A ile ağ oluştur");
            display_draw_text(6, 0, "STA bağlantısı kesilir");
            display_draw_text(8, 0, "İnternet paylaşımı yok");
            display_draw_text(14, 0, "SAĞ/A: aç  SOL: çık");
        }
        if (operation_failed) {
            display_draw_text_color(13, 0, "İşlem başarısız", DISPLAY_COLOR_ERROR);
        }
        display_flush();

        button_id_t event = poll_button_for_ticks(50);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            break;
        }
        if (event == BUTTON_UP && active) {
            // Share the running AP's credentials as a Wi-Fi QR: a phone that
            // scans it joins automatically, no manual password typing.
            // static, not on the stack: qr_code_t holds a 57x57 module buffer
            // (~3.2KB). On the stack it blew the main task stack the moment
            // this screen was entered (the compiler reserves the frame up
            // front), resetting the device when opening WiFi Ağım. Entered
            // only from the single-threaded menu loop, so static is safe.
            static qr_code_t qr;
            if (qr_encode_wifi(&qr, status.ssid, status.password)) {
                show_qr_screen(&qr, "Tara: WiFi'ye baglan");
            } else {
                display_clear();
                display_draw_text_color(2, 0, "QR olusturulamadi", DISPLAY_COLOR_ERROR);
                display_draw_text(4, 0, "Bir tusa bas");
                display_flush();
                wait_for_any_key();
            }
            continue;
        }
        if (event == BUTTON_RIGHT || event == BUTTON_PRESS) {
            bool ok = active ? c6_link_ap_stop() : c6_link_ap_start();
            operation_failed = !ok;
            if (!ok) {
                diag_record_error("WiFi Local AP", active ?
                                  "C6_AP_STOP_FAILED" : "C6_AP_START_FAILED");
            }
        }
    }
    menu_render(s_active_menu);
}


static void show_wifi_ap_details(const c6_monitor_ap_t *ap)
{
    display_clear();
    display_draw_text_centered(0, "WiFi Ağ Bilgisi", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, ap->ssid[0] ? ap->ssid : "(gizli ağ)");
    char line[DISPLAY_COLS + 1];
    snprintf(line, sizeof(line), "MAC %02X:%02X:%02X:%02X:%02X:%02X",
             ap->bssid[0], ap->bssid[1], ap->bssid[2],
             ap->bssid[3], ap->bssid[4], ap->bssid[5]);
    display_draw_text(4, 0, line);
    snprintf(line, sizeof(line), "Kanal %u   RSSI %d dBm", ap->channel, ap->rssi);
    display_draw_text(6, 0, line);
    snprintf(line, sizeof(line), "Güvenlik: %.7s", ap->sec);
    display_draw_text(7, 0, line);
    snprintf(line, sizeof(line), "Üretici: %.8s", ap->vendor);
    display_draw_text(8, 0, line);
    display_draw_text(11, 0, "Pasif izleme: bağlantı yok");
    display_draw_text(14, 0, "SOL: listeye dön");
    display_flush();
    while (true) {
        button_id_t event = poll_button_for_ticks(50);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            return;
        }
    }
}

static void show_bt_device_details(const c6_bt_device_t *device)
{
    display_clear();
    display_draw_text_centered(0, "BLE Aygıt Bilgisi", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, device->name[0] ? device->name : "(adsız aygıt)");
    char line[DISPLAY_COLS + 1];
    snprintf(line, sizeof(line), "MAC %02X:%02X:%02X:%02X:%02X:%02X",
             device->addr[0], device->addr[1], device->addr[2],
             device->addr[3], device->addr[4], device->addr[5]);
    display_draw_text(4, 0, line);
    snprintf(line, sizeof(line), "Sinyal: %d dBm", device->rssi);
    display_draw_text(6, 0, line);
    // Best-effort device kind (passive, from advertised UUIDs/appearance/vendor).
    if (device->kind[0]) {
        snprintf(line, sizeof(line), "Tur: %s", device->kind);
        display_draw_text_color(9, 0, line, DISPLAY_COLOR_OK);
    }
    // Decoded beacon frame, if this advertisement carried one. Passive parse of
    // bytes the device already broadcast -- see decode_beacon() in radio_ble.c.
    if (device->beacon_type == C6_BEACON_IBEACON) {
        display_draw_text_color(7, 0, "iBeacon", DISPLAY_COLOR_OK);
        display_draw_text(8, 0, device->beacon_info);
    } else if (device->beacon_type == C6_BEACON_EDDYSTONE) {
        display_draw_text_color(7, 0, device->beacon_info, DISPLAY_COLOR_OK);
    }
    display_draw_text(10, 0, "Bu sürüm yalnızca BLE tarar.");
    display_draw_text(11, 0, "Eşleştirme/bağlanma yok.");
    display_draw_text(14, 0, "SOL: listeye dön");
    display_flush();
    while (true) {
        button_id_t event = poll_button_for_ticks(50);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            return;
        }
    }
}

// Passive Wi-Fi Monitor: starts the C6's promiscuous channel-hop sniffer
// (see c6_link.h's c6_link_monitor_start() comment) and shows a live,
// polled list of APs seen (SSID/channel/RSSI) until BACK. Like the Wi-Fi
// network picker it scrolls, but it never exits on its own; only BACK stops it.
//
// Starting this disconnects the C6's STA connection and blocks every other
// C6 feature (WiFi Scan/Setup, Errors -> Send) for as long as the screen
// is open -- see c6_link.h. That's called out on-screen so it isn't a
// surprise, and c6_link_monitor_stop() is called on every exit path
// (BACK, or the start failing) so nothing can leave the C6 stuck in
// monitor mode after the user backs out.
static void action_wifi_monitor(void)
{
    if (c6_link_ap_is_running()) {
        display_clear();
        display_draw_text_centered(0, "WiFi İzleme [MON]", DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, "Önce WiFi Ağım'ı kapat");
        display_draw_text(6, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }
    // "[MON]" marks this as promiscuous Wi-Fi Monitor mode, the C6's radio
    // in receive-everything mode rather than station mode -- distinct
    // enough (drops the STA connection, can't scan/connect while running)
    // that the screen should say so at a glance, not just via the menu
    // label. See action_wifi_scan_test()'s "[STA]" comment for the
    // matching station-mode marker.
    display_clear();
    display_draw_text_centered(0, "WiFi İzleme [MON]", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Başlatılıyor...");
    display_draw_text_color(4, 0, "STA bağlantısı kesilir", DISPLAY_COLOR_DIM);
    display_flush();

    if (!c6_link_monitor_start()) {
        diag_record_error("WiFi Monitor", "C6_LINK_MONITOR_START_FAILED");
        display_clear();
        display_draw_text_centered(0, "WiFi İzleme [MON]", DISPLAY_COLOR_ACCENT);
        display_draw_text_color(2, 0, "Başlatılamadı", DISPLAY_COLOR_ERROR);
        display_draw_text(6, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }

    // static for the same reason as action_bt_scan's devices[]: a 32-entry AP
    // array on the main task stack is large enough to risk the stack-protection
    // fault seen with the BLE list. Entered only from the single-threaded menu
    // loop, so a static scratch buffer is safe.
    static c6_monitor_ap_t aps[C6_MONITOR_MAX_APS];
    int selected = 0;
    int top = 0;
    for (;;) {
        int count = c6_link_monitor_poll(aps, C6_MONITOR_MAX_APS);
        // Sort nearest-first by RSSI (higher = closer). Live list, so sort each
        // refresh. Insertion sort; small n. Local snapshot only.
        for (int a = 1; a < count; a++) {
            c6_monitor_ap_t key = aps[a];
            int b = a - 1;
            while (b >= 0 && aps[b].rssi < key.rssi) {
                aps[b + 1] = aps[b];
                b--;
            }
            aps[b + 1] = key;
        }
        if (count == 0) {
            selected = 0;
            top = 0;
        } else {
            if (selected >= count) {
                selected = count - 1;
            }
            list_clamp_scroll(selected, &top);
        }

        display_clear();
        char header[DISPLAY_COLS * 2 + 1];
        snprintf(header, sizeof(header), "WiFi İzleme [MON] (%d)", count);
        display_draw_text_centered(0, header, DISPLAY_COLOR_ACCENT);
        for (int i = 0; i < count - top && i < LIST_VISIBLE_ROWS; i++) {
            int index = top + i;
            char line[DISPLAY_COLS + 1];
            // Flag weak security: OPEN and WEP are insecure. A leading '!'
            // plus red text marks them so an audit of your own network's
            // surroundings spots them at a glance. Passive read of the beacon.
            const char *sec = aps[index].sec[0] ? aps[index].sec : "?";
            bool weak = (strcmp(sec, "OPEN") == 0) || (strcmp(sec, "WEP") == 0);
            snprintf(line, sizeof(line), "%c%c%.7s %.4s k%u %d",
                     index == selected ? '>' : ' ',
                     weak ? '!' : ' ',
                     aps[index].ssid[0] ? aps[index].ssid : "(gizli)",
                     sec, aps[index].channel, aps[index].rssi);
            display_draw_text_color(LIST_HEADER_ROWS + i, 0, line,
                                    weak ? DISPLAY_COLOR_ERROR : DISPLAY_COLOR_TEXT);
        }
        if (count == 0) {
            display_draw_text(2, 0, "Erişim noktası aranıyor...");
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, "YUK/AŞA SAĞ:bilgi SOL:çık");
        display_flush();

        button_id_t event = poll_button_for_ticks(50);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            break;
        } else if (event == BUTTON_UP && selected > 0) {
            selected--;
        } else if (event == BUTTON_DOWN && selected < count - 1) {
            selected++;
        } else if ((event == BUTTON_RIGHT || event == BUTTON_PRESS) && count > 0) {
            show_wifi_ap_details(&aps[selected]);
        }
    }

    c6_link_monitor_stop();
    menu_render(s_active_menu);
}

// Shared preamble for the passive-monitor-based screens below (channel map,
// frame stats). Shows the AP-conflict / start-failure messages exactly like
// action_wifi_monitor() and returns true only when the promiscuous monitor is
// running. On any false return the caller must not touch the radio and should
// just menu_render() back. Keeps the three monitor screens from each repeating
// the same start/error handling.
static bool monitor_screen_begin(const char *title)
{
    if (c6_link_ap_is_running()) {
        display_clear();
        display_draw_text_centered(0, title, DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, "Önce WiFi Ağım'ı kapat");
        display_draw_text(6, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        return false;
    }
    display_clear();
    display_draw_text_centered(0, title, DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Başlatılıyor...");
    display_draw_text_color(4, 0, "STA bağlantısı kesilir", DISPLAY_COLOR_DIM);
    display_flush();
    if (!c6_link_monitor_start()) {
        diag_record_error("WiFi Monitor", "C6_LINK_MONITOR_START_FAILED");
        display_clear();
        display_draw_text_centered(0, title, DISPLAY_COLOR_ACCENT);
        display_draw_text_color(2, 0, "Başlatılamadı", DISPLAY_COLOR_ERROR);
        display_draw_text(6, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        return false;
    }
    return true;
}

// Kanal ısı haritası: passive 2.4GHz channel-occupancy view. Runs the same
// promiscuous channel-hop monitor as WiFi İzleme, but instead of a device
// list it draws one horizontal bar per channel (1-13) sized by how many
// distinct APs are currently seen there, with the AP count and strongest RSSI
// beside it. No transmission -- it only visualizes the beacon list the monitor
// already builds. BACK/LEFT stops the monitor and exits.
static void action_wifi_channel_map(void)
{
    if (!monitor_screen_begin("Kanal Haritası [MON]")) {
        menu_render(s_active_menu);
        return;
    }

    // Bar geometry: label column on the left, bar fills the rest of the row.
    const int bar_x0 = 40;                       // px, after "K13 " label
    const int bar_max_w = DISPLAY_WIDTH_PX - bar_x0 - 4;
    for (;;) {
        c6_channel_stats_t stats;
        bool ok = c6_link_monitor_channel_stats(&stats);

        display_clear();
        display_draw_text_centered(0, "Kanal Haritası [MON]", DISPLAY_COLOR_ACCENT);
        if (ok) {
            // Find the busiest channel to scale the bars against (min 1 so an
            // all-quiet screen doesn't divide by zero).
            int max_count = 1;
            for (int ch = 1; ch <= C6_CHANNEL_COUNT; ch++) {
                if (stats.ap_count[ch] > max_count) {
                    max_count = stats.ap_count[ch];
                }
            }
            // 13 channels won't all fit as 16px rows under the header; pack two
            // channels per text row by drawing at a tighter pixel pitch.
            const int top_y = 18;
            const int pitch = (DISPLAY_HEIGHT_PX - top_y - 4) / C6_CHANNEL_COUNT;
            for (int ch = 1; ch <= C6_CHANNEL_COUNT; ch++) {
                int y = top_y + (ch - 1) * pitch;
                char lbl[8];
                snprintf(lbl, sizeof(lbl), "K%-2d", ch);
                display_draw_text_px(2, y, lbl, DISPLAY_COLOR_TEXT,
                                     display_get_background());
                int w = (stats.ap_count[ch] * bar_max_w) / max_count;
                if (stats.ap_count[ch] > 0 && w < 2) {
                    w = 2; // keep a nonzero channel visible
                }
                if (w > 0) {
                    display_fill_rect(bar_x0, y + 2, w, pitch - 3,
                                      DISPLAY_COLOR_ACCENT);
                }
                if (stats.ap_count[ch] > 0) {
                    char amt[12];
                    snprintf(amt, sizeof(amt), "%u/%ddBm",
                             stats.ap_count[ch], stats.best_rssi[ch]);
                    display_draw_text_px(bar_x0 + 2, y, amt,
                                         DISPLAY_COLOR_ACCENT_TEXT,
                                         DISPLAY_COLOR_ACCENT);
                }
            }
        } else {
            display_draw_text(2, 0, "Veri bekleniyor...");
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, "SOL: çık");
        display_flush();

        button_id_t event = poll_button_for_ticks(50);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            break;
        }
    }
    c6_link_monitor_stop();
    menu_render(s_active_menu);
}

// WiFi çerçeve istatistiği: passive 802.11 frame-type tally. Runs the monitor
// and shows cumulative counts of beacon / probe-req / probe-resp / other-mgmt /
// data / control frames seen across the channels the hopper visits. Receive
// only -- it classifies frames the radio already hears and transmits nothing.
// BACK/LEFT stops the monitor and exits.
static void action_wifi_frame_stats(void)
{
    if (!monitor_screen_begin("Çerçeve İstat [MON]")) {
        menu_render(s_active_menu);
        return;
    }

    for (;;) {
        c6_frame_stats_t st;
        bool ok = c6_link_monitor_frame_stats(&st);

        display_clear();
        display_draw_text_centered(0, "Çerçeve İstat [MON]", DISPLAY_COLOR_ACCENT);
        if (ok) {
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "Toplam: %lu", (unsigned long)st.total);
            display_draw_text_color(2, 0, line, DISPLAY_COLOR_OK);
            snprintf(line, sizeof(line), "Beacon      : %lu",
                     (unsigned long)st.mgmt_beacon);
            display_draw_text(4, 0, line);
            snprintf(line, sizeof(line), "Probe istek : %lu",
                     (unsigned long)st.mgmt_probe_req);
            display_draw_text(5, 0, line);
            snprintf(line, sizeof(line), "Probe yanıt : %lu",
                     (unsigned long)st.mgmt_probe_resp);
            display_draw_text(6, 0, line);
            snprintf(line, sizeof(line), "Diğer mgmt  : %lu",
                     (unsigned long)st.mgmt_other);
            display_draw_text(7, 0, line);
            snprintf(line, sizeof(line), "Veri        : %lu",
                     (unsigned long)st.data);
            display_draw_text(8, 0, line);
            snprintf(line, sizeof(line), "Kontrol     : %lu",
                     (unsigned long)st.ctrl);
            display_draw_text(9, 0, line);
            char chn[DISPLAY_COLS + 1];
            snprintf(chn, sizeof(chn), "Kanal: %d", c6_link_monitor_current_channel());
            display_draw_text_color(11, 0, chn, DISPLAY_COLOR_DIM);
        } else {
            display_draw_text(2, 0, "Veri bekleniyor...");
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, "SOL: çık");
        display_flush();

        button_id_t event = poll_button_for_ticks(50);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            break;
        }
    }
    c6_link_monitor_stop();
    menu_render(s_active_menu);
}

// Probe-Request capture: while the monitor runs, nearby client devices
// broadcast probe requests naming networks they want to join. This lists the
// distinct SSIDs seen, with a sighting count. Strictly passive -- it reads
// names devices broadcast on their own; it stores no device addresses and
// sends nothing. Useful for seeing what your own devices leak, or surveying a
// space you are authorized to assess.
static void action_wifi_probe_capture(void)
{
    if (!monitor_screen_begin("Probe Yakala [MON]")) {
        menu_render(s_active_menu);
        return;
    }

    static c6_probe_ssid_t probes[C6_PROBE_MAX]; // static: keep off the stack
    int selected = 0;
    int top = 0;
    for (;;) {
        int count = c6_link_monitor_probe_poll(probes, C6_PROBE_MAX);
        if (count == 0) {
            selected = 0;
            top = 0;
        } else {
            if (selected >= count) {
                selected = count - 1;
            }
            list_clamp_scroll(selected, &top);
        }

        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "Probe Yakala (%d)", count);
        display_draw_text_centered(0, header, DISPLAY_COLOR_ACCENT);
        for (int i = 0; i < count - top && i < LIST_VISIBLE_ROWS; i++) {
            int index = top + i;
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "%c%.22s x%u",
                     index == selected ? '>' : ' ',
                     probes[index].ssid, probes[index].count);
            display_draw_text(LIST_HEADER_ROWS + i, 0, line);
        }
        if (count == 0) {
            display_draw_text(2, 0, "Probe bekleniyor...");
            display_draw_text_color(4, 0, "Cihazlar arayinca dolar",
                                    DISPLAY_COLOR_DIM);
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, "YUK/ASA  SOL:cik");
        display_flush();

        button_id_t event = poll_button_for_ticks(50);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            break;
        } else if (event == BUTTON_UP && selected > 0) {
            selected--;
        } else if (event == BUTTON_DOWN && selected < count - 1) {
            selected++;
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
    if (c6_link_ap_is_running()) {
        display_clear();
        display_draw_text_centered(0, "BT Tarama", DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, "Önce WiFi Ağım'ı kapat");
        display_draw_text(6, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }
    display_clear();
    display_draw_text_centered(0, "BT Tarama", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Başlatılıyor...");
    display_flush();

    if (!c6_link_bt_scan_start()) {
        diag_record_error("BT Scan", "C6_LINK_BT_SCAN_START_FAILED");
        display_clear();
        display_draw_text_centered(0, "BT Tarama", DISPLAY_COLOR_ACCENT);
        display_draw_text_color(2, 0, "Başlatılamadı", DISPLAY_COLOR_ERROR);
        display_draw_text(6, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }

    // static, not on the stack: c6_bt_device_t grew (beacon fields) and 32 of
    // them is ~3KB, which overflowed the main task stack when combined with
    // snprintf() below (Guru Meditation "Stack protection fault" in _vfprintf_r
    // during BLE Keşif / BT Tara). This screen is only ever entered from the
    // single-threaded menu loop, so a static scratch buffer is safe here.
    static c6_bt_device_t devices[C6_BT_MAX_DEVICES];
    int selected = 0;
    int top = 0;
    for (;;) {
        int count = c6_link_bt_scan_poll(devices, C6_BT_MAX_DEVICES);
        // Sort the snapshot: named devices first, then, within each group,
        // nearest-first by RSSI (higher = closer). So the list reads as
        // [named, strongest..weakest] then [anonymous, strongest..weakest].
        // Insertion sort; the device count is small (<=32). Local copy only.
        for (int a = 1; a < count; a++) {
            c6_bt_device_t key = devices[a];
            bool key_named = key.name[0] != '\0';
            int b = a - 1;
            while (b >= 0) {
                bool b_named = devices[b].name[0] != '\0';
                // key should come before devices[b] if it's named and b isn't,
                // or (same named-group) it has a stronger RSSI.
                bool key_before = (key_named && !b_named) ||
                                  (key_named == b_named && key.rssi > devices[b].rssi);
                if (!key_before) {
                    break;
                }
                devices[b + 1] = devices[b];
                b--;
            }
            devices[b + 1] = key;
        }
        if (count == 0) {
            selected = 0;
            top = 0;
        } else {
            if (selected >= count) {
                selected = count - 1;
            }
            list_clamp_scroll(selected, &top);
        }

        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "BT Tarama (%d)", count);
        display_draw_text_centered(0, header, DISPLAY_COLOR_ACCENT);
        for (int i = 0; i < count - top && i < LIST_VISIBLE_ROWS; i++) {
            int index = top + i;
            char line[DISPLAY_COLS + 1];
            // A single-char tag marks a decoded beacon (i=iBeacon, E=Eddystone)
            // so beacons stand out in the list before opening details.
            char tag = ' ';
            if (devices[index].beacon_type == C6_BEACON_IBEACON) {
                tag = 'i';
            } else if (devices[index].beacon_type == C6_BEACON_EDDYSTONE) {
                tag = 'E';
            }
            // Most BLE devices (and phones especially) don't broadcast a name;
            // showing "(adsız)" for all of them makes them indistinguishable.
            // Fall back to the last 3 MAC bytes so each device is identifiable.
            char label[18];
            if (devices[index].name[0]) {
                snprintf(label, sizeof(label), "%.15s", devices[index].name);
            } else {
                snprintf(label, sizeof(label), "%02X:%02X:%02X",
                         devices[index].addr[3], devices[index].addr[4],
                         devices[index].addr[5]);
            }
            snprintf(line, sizeof(line), "%c%c%.15s %ddBm",
                     index == selected ? '>' : ' ', tag, label,
                     devices[index].rssi);
            display_draw_text(LIST_HEADER_ROWS + i, 0, line);
        }
        if (count == 0) {
            display_draw_text(2, 0, "BLE aygıtı aranıyor...");
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, "YUK/AŞA SAĞ:bilgi SOL:çık");
        display_flush();

        button_id_t event = poll_button_for_ticks(50);
        if (event == BUTTON_BACK || event == BUTTON_LEFT) {
            break;
        } else if (event == BUTTON_UP && selected > 0) {
            selected--;
        } else if (event == BUTTON_DOWN && selected < count - 1) {
            selected++;
        } else if ((event == BUTTON_RIGHT || event == BUTTON_PRESS) && count > 0) {
            show_bt_device_details(&devices[selected]);
        }
    }

    c6_link_bt_scan_stop();
    menu_render(s_active_menu);
}

// --- BLE Radar (yön + mesafe haritalama) -----------------------------------
// Locates BLE devices around the user WITHOUT any extra hardware (no compass,
// no gyro, single antenna). It exploits body-shadowing while the user turns in
// place: as you rotate holding the device, each target's RSSI peaks when you
// are facing roughly toward it. We time the rotation instead of measuring an
// angle -- the user marks the start and end of one full 360 turn, so elapsed
// time maps linearly to bearing (0..360). For every device we remember the
// moment its RSSI was strongest; that moment, as a fraction of the full turn,
// is its bearing. RSSI magnitude sets the distance from center (stronger =
// closer). The result is drawn as a radar: center = you, rings = signal
// strength (-40/-60/-80 dBm), blips = devices at their estimated bearing.
//
// This is a coarse estimate, not a metric fix: RSSI-to-distance is noisy and
// bearing assumes a steady turn speed. It is genuinely useful for "which
// direction is that beacon" but should not be read as exact meters/degrees.

#define RADAR_MAX_TARGETS C6_BT_MAX_DEVICES
// RSSI range mapped onto the radar radius. Anything >= NEAR sits at the center
// ring, anything <= FAR sits at the outer ring.
#define RADAR_RSSI_NEAR (-40)
#define RADAR_RSSI_FAR  (-90)

// Per-target peak tracking, kept in static storage (parallel to the BLE list)
// so the large arrays stay off the main task stack -- same lesson as the BLE
// scan crash fix.
static uint8_t  s_radar_addr[RADAR_MAX_TARGETS][6];
static int8_t   s_radar_peak_rssi[RADAR_MAX_TARGETS]; // strongest seen (for the footer)
static int64_t  s_radar_peak_us[RADAR_MAX_TARGETS];   // time of that peak
static int8_t   s_radar_live_rssi[RADAR_MAX_TARGETS]; // most recent RSSI -> live distance
static int64_t  s_radar_seen_us[RADAR_MAX_TARGETS];   // last time this target was heard
static char     s_radar_name[RADAR_MAX_TARGETS][C6_BT_NAME_MAX_LEN + 1];
static int      s_radar_target_count;

// Weighted bearing estimate. Instead of trusting the single strongest sample
// (one noisy reading could throw the angle off), every calibration sample is
// added as a vector: direction = the turn angle at that instant, length = a
// weight rising with RSSI. Summing across the whole turn -- and across several
// turns -- averages the noise out. The resolved bearing is atan2 of the sums.
// A higher total weight also means a more trustworthy fix, shown to the user.
static float    s_radar_vec_x[RADAR_MAX_TARGETS]; // sum of w*cos(angle)
static float    s_radar_vec_y[RADAR_MAX_TARGETS]; // sum of w*sin(angle)
static float    s_radar_vec_w[RADAR_MAX_TARGETS]; // sum of weights (fix confidence)
static uint16_t s_radar_last_seq[RADAR_MAX_TARGETS]; // dedup: only fold fresh samples
static float    s_radar_bearing[RADAR_MAX_TARGETS];  // resolved bearing, radians
// Second calibration point (for triangulation): the bearing measured after the
// user steps forward. The two bearings + the step give an (x,y) position.
static float    s_radar_bearing2[RADAR_MAX_TARGETS];
static float    s_radar_vec2_x[RADAR_MAX_TARGETS];
static float    s_radar_vec2_y[RADAR_MAX_TARGETS];
static float    s_radar_vec2_w[RADAR_MAX_TARGETS];
static bool     s_radar_has_fix2;
static float    s_radar_pos_x[RADAR_MAX_TARGETS];  // triangulated position, in steps
static float    s_radar_pos_y[RADAR_MAX_TARGETS];
static bool     s_radar_pos_valid[RADAR_MAX_TARGETS];

// A target not heard for this long is treated as gone and dimmed/dropped from
// the live view (BLE advertising is intermittent, so allow a few seconds).
#define RADAR_STALE_US (6 * 1000000)

// Fills `order` with target indices sorted nearest-first by live RSSI (higher
// RSSI = closer = earlier). Simple insertion sort; target count is small (<=32).
// Returns the number of entries written (== s_radar_target_count).
static int radar_sorted_order(int *order)
{
    int n = s_radar_target_count;
    for (int i = 0; i < n; i++) {
        order[i] = i;
    }
    for (int i = 1; i < n; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && s_radar_live_rssi[order[j]] < s_radar_live_rssi[key]) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    return n;
}

// Map an RSSI (dBm, negative) to a 0..1 distance fraction from center.
static float radar_distance_frac(int8_t rssi)
{
    if (rssi >= RADAR_RSSI_NEAR) {
        return 0.0f;
    }
    if (rssi <= RADAR_RSSI_FAR) {
        return 1.0f;
    }
    return (float)(RADAR_RSSI_NEAR - rssi) /
           (float)(RADAR_RSSI_NEAR - RADAR_RSSI_FAR);
}

// RSSI -> vector weight for the bearing sum. Stronger signal = the user is
// facing more directly at the target, so it should pull the bearing harder.
// Shift so the weakest usable signal contributes almost nothing and near
// signals dominate; clamped to stay positive.
static float radar_sample_weight(int8_t rssi)
{
    float w = (float)(rssi - RADAR_RSSI_FAR); // 0 at the far edge, ~50 up close
    if (w < 1.0f) {
        w = 1.0f;
    }
    return w * w; // square it so the strongest headings dominate the average
}

// Fold new poll results into the trackers. `sweep_ang` is the direction the
// user is facing right now (radians, only meaningful while calibrating).
// track_peak true = a calibration turn is running: add each FRESH sample
// (seen_seq changed since last fold) to the target's bearing vector. slot
// picks which vector set to accumulate into (0 = first fix, 1 = second fix for
// triangulation). track_peak false = live phase: just refresh distance.
static void radar_update(const c6_bt_device_t *devs, int count, int64_t now_us,
                         bool track_peak, float sweep_ang, int slot)
{
    for (int i = 0; i < count; i++) {
        int found = -1;
        for (int j = 0; j < s_radar_target_count; j++) {
            if (memcmp(s_radar_addr[j], devs[i].addr, 6) == 0) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            if (s_radar_target_count >= RADAR_MAX_TARGETS) {
                continue;
            }
            found = s_radar_target_count++;
            memcpy(s_radar_addr[found], devs[i].addr, 6);
            s_radar_peak_rssi[found] = devs[i].rssi;
            s_radar_peak_us[found] = now_us;
            s_radar_live_rssi[found] = devs[i].rssi; // seed the smoothed value
            s_radar_vec_x[found] = 0.0f;
            s_radar_vec_y[found] = 0.0f;
            s_radar_vec_w[found] = 0.0f;
            s_radar_vec2_x[found] = 0.0f;
            s_radar_vec2_y[found] = 0.0f;
            s_radar_vec2_w[found] = 0.0f;
            s_radar_last_seq[found] = 0;
            s_radar_pos_valid[found] = false;
            if (devs[i].name[0]) {
                snprintf(s_radar_name[found], sizeof(s_radar_name[found]), "%s",
                         devs[i].name);
            } else {
                // No advertised name: identify by the last 3 MAC bytes.
                snprintf(s_radar_name[found], sizeof(s_radar_name[found]),
                         "%02X:%02X:%02X", devs[i].addr[3], devs[i].addr[4],
                         devs[i].addr[5]);
            }
        } else if (devs[i].name[0] && s_radar_name[found][0] == '?') {
            // Learned a name later (active scan response); upgrade the label.
            snprintf(s_radar_name[found], sizeof(s_radar_name[found]), "%s",
                     devs[i].name);
        }

        // Only fold a sample into the bearing sum if the radio actually heard
        // this device again since the last fold -- otherwise a device polled 10
        // times a second but only advertising twice a second would count the
        // same stale reading many times and bias the vector toward wherever the
        // user happened to be pointing during the gap.
        bool fresh = (devs[i].seen_seq != s_radar_last_seq[found]);
        s_radar_last_seq[found] = devs[i].seen_seq;

        if (track_peak && fresh) {
            float w = radar_sample_weight(devs[i].rssi);
            if (slot == 0) {
                s_radar_vec_x[found] += w * cosf(sweep_ang);
                s_radar_vec_y[found] += w * sinf(sweep_ang);
                s_radar_vec_w[found] += w;
            } else {
                s_radar_vec2_x[found] += w * cosf(sweep_ang);
                s_radar_vec2_y[found] += w * sinf(sweep_ang);
                s_radar_vec2_w[found] += w;
            }
            if (devs[i].rssi > s_radar_peak_rssi[found]) {
                s_radar_peak_rssi[found] = devs[i].rssi;
                s_radar_peak_us[found] = now_us;
            }
        }
        // Smooth the live RSSI so blips don't jitter with raw radio noise.
        int old = s_radar_live_rssi[found];
        int sample = devs[i].rssi;
        s_radar_live_rssi[found] = (int8_t)(old + (sample - old) / 4);
        s_radar_seen_us[found] = now_us;
    }
}

// After a calibration turn, resolve each target's accumulated vector into a
// single bearing (radians). slot 0 stores into s_radar_bearing, slot 1 into
// s_radar_bearing2. Targets with too little total weight (barely heard) keep
// their previous bearing rather than snapping to a noise direction.
static void radar_resolve_bearings(int slot)
{
    for (int i = 0; i < s_radar_target_count; i++) {
        float vx = (slot == 0) ? s_radar_vec_x[i] : s_radar_vec2_x[i];
        float vy = (slot == 0) ? s_radar_vec_y[i] : s_radar_vec2_y[i];
        float w  = (slot == 0) ? s_radar_vec_w[i] : s_radar_vec2_w[i];
        if (w <= 0.0f || (vx == 0.0f && vy == 0.0f)) {
            continue;
        }
        float ang = atan2f(vy, vx);
        if (slot == 0) {
            s_radar_bearing[i] = ang;
        } else {
            s_radar_bearing2[i] = ang;
        }
    }
}

// Triangulate positions from the two bearing fixes. The user stepped forward
// by `step` (in arbitrary "steps"; only relative distances matter) along the
// direction they first faced (radar's 12 o'clock = +Y). Fix 1 is taken from
// the origin, fix 2 from (0, step). Each target sits where the two bearing
// rays cross. Bearings too close to parallel give no usable crossing and are
// left as bearing-only.
static void radar_triangulate(float step)
{
    for (int i = 0; i < s_radar_target_count; i++) {
        s_radar_pos_valid[i] = false;
        // Radar angle convention: 0 rad = 12 o'clock = +Y (forward), growing
        // clockwise. Convert each bearing to a unit ray in (x=right, y=forward).
        float a1 = s_radar_bearing[i];
        float a2 = s_radar_bearing2[i];
        float d1x = sinf(a1), d1y = cosf(a1); // from origin (0,0)
        float d2x = sinf(a2), d2y = cosf(a2); // from (0, step)
        // Solve  t1*d1 = (0,step) + t2*d2  for t1. Cross-product denominator.
        float denom = d1x * d2y - d1y * d2x;
        if (fabsf(denom) < 0.02f) {
            continue; // rays nearly parallel -> target far away / no crossing
        }
        float t1 = (0.0f * d2y - step * d2x) / (-denom);
        // Equivalent stable form:
        t1 = (step * d2x) / denom;
        if (t1 <= 0.0f) {
            continue; // crossing is behind the user -> unreliable
        }
        s_radar_pos_x[i] = t1 * d1x;
        s_radar_pos_y[i] = t1 * d1y;
        s_radar_pos_valid[i] = true;
    }
}

#define RADAR_TWO_PI 6.28318531f
#define RADAR_HALF_PI 1.57079633f

// Convert a radar bearing (0 = 12 o'clock, clockwise, radians) to a screen
// angle for cos/sin where +x = right and +y = down. Screen 12 o'clock is
// -HALF_PI, and clockwise on screen matches increasing bearing, so:
static inline float radar_screen_angle(float bearing)
{
    return bearing - RADAR_HALF_PI;
}

// Draw the radar. Bearings come from the resolved vector sums
// (s_radar_bearing). `sweep_frac` in [0,1] draws the rotating beam during a
// calibration turn; pass < 0 to omit it. `show_pos` true switches the blips
// from bearing+RSSI placement to the triangulated (x,y) map.
static void radar_render(const char *title, int highlight, float sweep_frac,
                         bool show_pos)
{
    const int cx = DISPLAY_WIDTH_PX / 2;
    const int cy = DISPLAY_HEIGHT_PX / 2 + 8; // leave room for the header
    const int r_outer = 104;

    display_clear();
    display_draw_text_centered(0, title, DISPLAY_COLOR_ACCENT);

    display_draw_circle(cx, cy, r_outer, DISPLAY_COLOR_DIM);
    display_draw_circle(cx, cy, r_outer * 2 / 3, DISPLAY_COLOR_DIM);
    display_draw_circle(cx, cy, r_outer / 3, DISPLAY_COLOR_DIM);
    display_draw_line(cx - r_outer, cy, cx + r_outer, cy, DISPLAY_COLOR_DIM);
    display_draw_line(cx, cy - r_outer, cx, cy + r_outer, DISPLAY_COLOR_DIM);

    // Rotating sweep line during calibration. sweep_frac is a fraction of the
    // turn; the beam sits at that heading.
    if (sweep_frac >= 0.0f) {
        float sa = radar_screen_angle(sweep_frac * RADAR_TWO_PI);
        int sx = cx + (int)(cosf(sa) * r_outer);
        int sy = cy + (int)(sinf(sa) * r_outer);
        display_draw_line(cx, cy, sx, sy, DISPLAY_COLOR_ACCENT);
    }

    // Sonar-style "ping": a ring expands out from the centre and fades,
    // looping continuously -- the same expanding-circle look as the boot
    // splash, but tied to wall-clock time so it animates smoothly across
    // repeated radar_render() calls instead of a fixed frame count.
    {
        const int64_t period_us = 2200000; // one full expand-and-fade cycle
        int64_t phase = esp_timer_get_time() % period_us;
        float frac = (float)phase / (float)period_us; // 0..1
        int ring_r = (int)(frac * r_outer);
        if (ring_r > 2) {
            display_color_t ring_col = (frac < 0.4f) ? DISPLAY_COLOR_OK
                                      : (frac < 0.75f) ? DISPLAY_RGB(4, 24, 18)
                                                        : DISPLAY_COLOR_DIM;
            display_draw_circle(cx, cy, ring_r, ring_col);
        }
    }

    display_fill_circle(cx, cy, 3, DISPLAY_COLOR_OK); // you are here

    // For the position map, find the farthest triangulated target so the whole
    // set fits inside the outer ring (auto-scale).
    float pos_scale = 0.0f;
    if (show_pos) {
        float maxd = 0.0f;
        for (int i = 0; i < s_radar_target_count; i++) {
            if (!s_radar_pos_valid[i]) continue;
            float d = sqrtf(s_radar_pos_x[i] * s_radar_pos_x[i] +
                            s_radar_pos_y[i] * s_radar_pos_y[i]);
            if (d > maxd) maxd = d;
        }
        if (maxd > 0.0f) pos_scale = (float)(r_outer - 6) / maxd;
    }

    int order[RADAR_MAX_TARGETS];
    int n = radar_sorted_order(order);
    int hi_real = (highlight >= 0 && highlight < n) ? order[highlight] : -1;

    int64_t now = esp_timer_get_time();
    for (int oi = 0; oi < n; oi++) {
        int i = order[oi];
        int px, py;
        bool placed = false;
        if (show_pos && s_radar_pos_valid[i] && pos_scale > 0.0f) {
            // Triangulated map: +x = right, +y = forward (up on screen).
            px = cx + (int)(s_radar_pos_x[i] * pos_scale);
            py = cy - (int)(s_radar_pos_y[i] * pos_scale);
            placed = true;
        } else {
            // Bearing + RSSI-distance placement.
            float ang = radar_screen_angle(s_radar_bearing[i]);
            float dist = radar_distance_frac(s_radar_live_rssi[i]);
            px = cx + (int)(cosf(ang) * dist * r_outer);
            py = cy + (int)(sinf(ang) * dist * r_outer);
            placed = true;
        }
        if (!placed) continue;
        bool stale = (now - s_radar_seen_us[i]) > RADAR_STALE_US;
        display_color_t c;
        if (stale) {
            c = DISPLAY_COLOR_DIM;
        } else if (i == hi_real) {
            c = DISPLAY_COLOR_ACCENT;
        } else {
            c = DISPLAY_RGB(6, 40, 31);
        }
        display_fill_circle(px, py, (i == hi_real) ? 4 : 3, c);
    }

    if (n > 0 && hi_real >= 0) {
        char line[DISPLAY_COLS + 1];
        int deg = (int)(s_radar_bearing[hi_real] * 180.0f / 3.14159265f);
        deg = ((deg % 360) + 360) % 360;
        snprintf(line, sizeof(line), ">#%d %.10s", highlight + 1,
                 s_radar_name[hi_real]);
        display_draw_text(DISPLAY_ROWS - 2, 0, line);
        if (show_pos && s_radar_pos_valid[hi_real]) {
            float d = sqrtf(s_radar_pos_x[hi_real] * s_radar_pos_x[hi_real] +
                            s_radar_pos_y[hi_real] * s_radar_pos_y[hi_real]);
            snprintf(line, sizeof(line), " %d der  ~%d adim  %d dBm", deg,
                     (int)(d + 0.5f), s_radar_live_rssi[hi_real]);
        } else {
            snprintf(line, sizeof(line), " ~%d derece  %d dBm", deg,
                     s_radar_live_rssi[hi_real]);
        }
        display_draw_text(DISPLAY_ROWS - 1, 0, line);
    } else {
        char line[DISPLAY_COLS + 1];
        int shown = s_radar_target_count > 99 ? 99 : s_radar_target_count;
        snprintf(line, sizeof(line), "%2d hedef  SAG/SOL", shown);
        display_draw_text(DISPLAY_ROWS - 1, 0, line);
    }
    display_flush();
}

// How many full turns one calibration fix averages over. More turns = more
// samples per target = a steadier bearing, at the cost of a longer spin.
#define RADAR_CAL_TURNS 2
// Nominal seconds per turn. The sweep beam and the sample-to-angle mapping use
// this as the assumed steady turn rate; the user just keeps a matching pace.
#define RADAR_TURN_SECONDS 12

// Result of one calibration phase.
typedef enum { RADAR_CAL_OK, RADAR_CAL_CANCEL } radar_cal_result_t;

// Shared poll buffer: the radio-specific poll wrapper fills this and returns a
// count, so radar_run_calibration/radar_multiphase stay radio-agnostic.
static c6_bt_device_t s_radar_poll_buf[C6_BT_MAX_DEVICES];

// Runs one calibration spin: RADAR_CAL_TURNS turns, folding every fresh sample
// into vector slot `slot`. `poll` fills the device buffer and returns a count.
// Shows a live sweep and turn counter. Returns CANCEL if the user backs out.
typedef int (*radar_poll_fn)(void);
static radar_cal_result_t radar_run_calibration(const char *title, int slot,
                                                radar_poll_fn poll)
{
    int64_t start_us = esp_timer_get_time();
    int64_t total_us = (int64_t)RADAR_CAL_TURNS * RADAR_TURN_SECONDS * 1000000;
    for (;;) {
        int64_t now = esp_timer_get_time();
        int64_t elapsed = now - start_us;
        if (elapsed >= total_us) {
            return RADAR_CAL_OK; // planned turns complete
        }
        // Angle within the CURRENT turn: fraction of one turn, wrapped.
        float turn_frac = (float)((elapsed / 1000) %
                                  (RADAR_TURN_SECONDS * 1000)) /
                          (float)(RADAR_TURN_SECONDS * 1000);
        float sweep_ang = turn_frac * RADAR_TWO_PI; // 0 = start heading

        int count = poll();
        // poll() writes into the shared radar device/AP buffer; radar_update is
        // called by the caller-specific wrapper below via s_radar_poll_buf.
        radar_update(s_radar_poll_buf, count, now, true, sweep_ang, slot);

        radar_render(title, -1, turn_frac, false);
        char line[DISPLAY_COLS + 1];
        // Bound each value into a single decimal digit / two digits so the
        // fixed-width format can never truncate (keeps -Werror happy).
        unsigned turn_no = (unsigned)(elapsed /
                           (RADAR_TURN_SECONDS * 1000000)) + 1u;
        turn_no %= 10u;
        unsigned total_secs = (unsigned)((total_us - elapsed) / 1000000) + 1u;
        total_secs %= 100u;
        unsigned tc = (unsigned)s_radar_target_count % 100u;
        snprintf(line, sizeof(line), "Tur %u/%u %02us %02u hd",
                 turn_no, (unsigned)RADAR_CAL_TURNS % 10u, total_secs, tc);
        display_draw_text(1, 0, line);
        display_draw_text_color(DISPLAY_ROWS - 1, 0, "Sabit hizda don  SOL:iptal",
                                DISPLAY_COLOR_DIM);
        display_flush();

        button_id_t e = poll_button_for_ticks(30);
        if (e == BUTTON_BACK || e == BUTTON_LEFT) {
            return RADAR_CAL_CANCEL;
        }
        if (e == BUTTON_RIGHT) { // PRESS excluded: unverified/floating GPIO3
            return RADAR_CAL_OK; // let the user end early if they've done a turn
        }
    }
}

static int radar_poll_ble(void)
{
    return c6_link_bt_scan_poll(s_radar_poll_buf, C6_BT_MAX_DEVICES);
}

// Waits on a prompt screen for RIGHT (go) or LEFT (cancel). Returns true on go.
static bool radar_prompt(const char *title, const char *l2, const char *l3,
                         const char *l4, const char *l6)
{
    display_clear();
    display_draw_text_centered(0, title, DISPLAY_COLOR_ACCENT);
    if (l2) display_draw_text(2, 0, l2);
    if (l3) display_draw_text(3, 0, l3);
    if (l4) display_draw_text(4, 0, l4);
    if (l6) display_draw_text(6, 0, l6);
    display_draw_text_color(DISPLAY_ROWS - 1, 0, "SAG: devam   SOL: cik",
                            DISPLAY_COLOR_DIM);
    display_flush();
    for (;;) {
        button_id_t e = poll_button_for_ticks(50);
        if (e == BUTTON_BACK || e == BUTTON_LEFT) return false;
        if (e == BUTTON_RIGHT) return true; // PRESS excluded (floating GPIO3)
    }
}

// The full multi-phase radar flow, shared by BLE and WiFi. `poll` is the
// radio-specific poll wrapper (fills s_radar_poll_buf, returns count).
//
// Flow: fix 1 (spin) -> live bearing radar. From there the user can press UP to
// add fix 2: step forward, spin again, and the two fixes triangulate an (x,y)
// position map. RIGHT cycles the highlighted target, LEFT exits.
static void radar_multiphase(const char *title, radar_poll_fn poll)
{
    bool skip_first_cal = false; // true right after a fix-2 spin: go live directly

    for (;;) {
        if (!skip_first_cal) {
            // ---- Phase 1: first calibration spin ----
            if (!radar_prompt(title, "Cihazi duz tut.", "Sabit hizda kendi",
                              "etrafinda 2 tur don.", "Basla: SAG")) {
                return;
            }
            s_radar_target_count = 0;
            s_radar_has_fix2 = false;
            if (radar_run_calibration(title, 0, poll) == RADAR_CAL_CANCEL) {
                continue; // back to the prompt
            }
            radar_resolve_bearings(0);
        }
        skip_first_cal = false;

        // ---- Phase 2: live bearing radar (or position map if fix 2 is in) ----
        int highlight = s_radar_target_count > 0 ? 0 : -1;
        bool want_fix2 = false;
        bool exit_all = false;
        for (;;) {
            int64_t now = esp_timer_get_time();
            int count = poll();
            radar_update(s_radar_poll_buf, count, now, false, 0.0f, 0);
            radar_render(title, highlight, -1.0f, s_radar_has_fix2);
            display_draw_text_color(1, 0,
                                    s_radar_has_fix2 ? "Konum haritasi  UP:tekrar" :
                                    "UP:konum bul  SAG:sec",
                                    DISPLAY_COLOR_DIM);
            display_flush();

            button_id_t e = poll_button_for_ticks(50);
            if (e == BUTTON_BACK || e == BUTTON_LEFT) { exit_all = true; break; }
            // Only RIGHT cycles the target. PRESS (GPIO3) is deliberately NOT
            // accepted here: its wire is unverified and a floating pin emits
            // spurious PRESS events, which made the highlight jump on its own.
            if (e == BUTTON_RIGHT && s_radar_target_count > 0) {
                highlight = (highlight + 1) % s_radar_target_count;
            }
            if (e == BUTTON_UP) { want_fix2 = true; break; }
        }
        if (exit_all) return;
        if (!want_fix2) continue;

        // ---- Phase 3: second fix for triangulation ----
        // If a fix 2 already exists, UP means "redo from scratch" -> full restart.
        if (s_radar_has_fix2) {
            continue; // back to phase 1 prompt for a fresh pair of fixes
        }
        if (!radar_prompt(title, "Duz ileri ~5 adim", "yuru, sonra ayni",
                          "yone bakip 2 tur don.", "Basla: SAG")) {
            skip_first_cal = true; // user skipped fix 2: return to the live view
            continue;
        }
        if (radar_run_calibration(title, 1, poll) == RADAR_CAL_CANCEL) {
            skip_first_cal = true;
            continue;
        }
        radar_resolve_bearings(1);
        radar_triangulate(5.0f); // ~5 steps forward between the two fixes
        s_radar_has_fix2 = true;
        skip_first_cal = true; // go straight to the live position map
    }
}

static void action_bt_radar(void)
{
    if (c6_link_ap_is_running()) {
        display_clear();
        display_draw_text_centered(0, "BLE Radar", DISPLAY_COLOR_ACCENT);
        display_draw_text(2, 0, "Önce WiFi Ağım'ı kapat");
        display_draw_text(6, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }
    display_clear();
    display_draw_text_centered(0, "BLE Radar", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "BLE başlatılıyor...");
    display_flush();

    if (!c6_link_bt_scan_start()) {
        diag_record_error("BLE Radar", "C6_LINK_BT_SCAN_START_FAILED");
        display_clear();
        display_draw_text_centered(0, "BLE Radar", DISPLAY_COLOR_ACCENT);
        display_draw_text_color(2, 0, "Başlatılamadı", DISPLAY_COLOR_ERROR);
        display_draw_text(6, 0, "Bir tuşa bas");
        display_flush();
        wait_for_any_key();
        menu_render(s_active_menu);
        return;
    }

    radar_multiphase("BLE Radar", radar_poll_ble);

    c6_link_bt_scan_stop();
    menu_render(s_active_menu);
}

// --- WiFi Radar (yön + mesafe haritalama, Access Point'ler için) -----------
// Reuses the whole BLE radar engine (radar_multiphase and friends). The only
// difference is the poll source: the WiFi monitor's AP list is bridged into the
// shared s_radar_poll_buf by mapping BSSID -> addr, SSID -> name, and carrying
// the AP's seen_seq so the same fresh-sample dedup works. See radar_multiphase.
static int radar_poll_wifi(void)
{
    static c6_monitor_ap_t aps[C6_MONITOR_MAX_APS];
    int count = c6_link_monitor_poll(aps, C6_MONITOR_MAX_APS);
    if (count > C6_BT_MAX_DEVICES) {
        count = C6_BT_MAX_DEVICES; // shared buffer is BT-sized
    }
    for (int i = 0; i < count; i++) {
        c6_bt_device_t *d = &s_radar_poll_buf[i];
        memset(d, 0, sizeof(*d));
        memcpy(d->addr, aps[i].bssid, 6);
        d->rssi = aps[i].rssi;
        d->seen_seq = aps[i].seen_seq;
        if (aps[i].ssid[0]) {
            // name is one byte shorter than an SSID field; truncate to fit.
            snprintf(d->name, sizeof(d->name), "%.*s",
                     (int)sizeof(d->name) - 1, aps[i].ssid);
        }
        // No SSID -> leave name empty so radar_update falls back to MAC tail.
    }
    return count;
}

static void action_wifi_radar(void)
{
    if (!monitor_screen_begin("WiFi Radar")) {
        menu_render(s_active_menu);
        return;
    }
    radar_multiphase("WiFi Radar", radar_poll_wifi);
    c6_link_monitor_stop();
    menu_render(s_active_menu);
}

static void action_about(void)
{
    display_clear();
    display_draw_text_centered(0, DEVICE_NAME, DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Makeshiftflipper");
    display_draw_text(3, 0, "ESP32-C6-Pico 4MB");
    display_draw_text(5, 0, "RFID / NFC (13.56 & 125k)");
    display_draw_text(6, 0, "Kızılötesi TX/RX + öğrenme");
    display_draw_text(7, 0, "WiFi tarama/kurulum/izleme");
    display_draw_text(8, 0, "Bluetooth LE tarama");
    display_draw_text_color(DISPLAY_ROWS - 2, 0, "github.com/ErdemWilkinson", DISPLAY_COLOR_DIM);
    display_draw_text_color(DISPLAY_ROWS - 1, 0, "Bir tuşa bas...", DISPLAY_COLOR_DIM);
    display_flush();
    wait_for_any_key();
}

// Scope reminder for the passive radio tools grouped under Security Lab.
// This screen does not start radio activity or change radio state.
static void action_security_lab_guide(void)
{
    display_clear();
    display_draw_text_centered(0, "SecLab", DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, "Yalnızca kendi cihazlarında");
    display_draw_text(3, 0, "veya izinli cihazlarda dene.");
    display_draw_text(5, 0, "WiFi/BLE yalnızca dinler.");
    display_draw_text(6, 0, "Bağlantı kesme/enjeksiyon yok.");
    display_draw_text(8, 0, "Kurtarma testlerinde");
    display_draw_text(9, 0, "yalıtılmış test ağını kullan.");
    display_draw_text(DISPLAY_ROWS - 1, 0, "Bir tuşa bas");
    display_flush();
    wait_for_any_key();
    menu_render(s_active_menu);
}

// Red warning splash shown once on entering the Hacking menu. Purely a
// scope/consent reminder -- it starts no radio activity. A key press drops
// into the (red-tinted) Hacking menu.
static void show_hacking_intro(void)
{
    display_set_background(DISPLAY_BG_HACKING);
    display_clear();
    display_draw_text_centered(0, "== HACKING ==", DISPLAY_COLOR_ERROR);
    display_draw_text_color(2, 0, "Yalnızca izinli kullanım.", DISPLAY_COLOR_ERROR);
    display_draw_text_color(4, 0, "Kendi veya test izni olan", DISPLAY_COLOR_HACKING_TEXT);
    display_draw_text_color(5, 0, "cihazlarda kullan.", DISPLAY_COLOR_HACKING_TEXT);
    display_draw_text_color(7, 0, "Buradaki araçlar yalnızca", DISPLAY_COLOR_HACKING_TEXT);
    display_draw_text_color(8, 0, "alıcıdır (RX).", DISPLAY_COLOR_HACKING_TEXT);
    display_draw_text_color(9, 0, "Enjeksiyon/saldırı yok.", DISPLAY_COLOR_HACKING_TEXT);
    display_draw_text_color(DISPLAY_ROWS - 1, 0, "Bir tuşa bas", DISPLAY_COLOR_HACKING_ACCENT);
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
        snprintf(out, out_cap, "%lld sn once", (long long)age_s);
    } else if (age_s < 3600) {
        snprintf(out, out_cap, "%lld dk once", (long long)(age_s / 60));
    } else {
        snprintf(out, out_cap, "%lld sa once", (long long)(age_s / 3600));
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
    // static: the 24-entry history array is ~2KB, too large for the small main
    // task stack. Called only from the single-threaded UI, so static is safe.
    static diag_entry_t entries[DIAG_HISTORY_CAPACITY];
    int count = diag_get_history(entries, DIAG_HISTORY_CAPACITY);

    int top = 0;
    for (;;) {
        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "Hatalar (%d)", count);
        display_draw_text_centered(0, header, DISPLAY_COLOR_ACCENT);

        if (count == 0) {
            display_draw_text(2, 0, "Kayıtlı hata yok");
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
        display_draw_text(DISPLAY_ROWS - 1, 0, "GERİ: çık");
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
// The physical LEFT switch emits BUTTON_BACK. It returns from a submenu
// to its parent (menu_link_submenu() below) or exits the active screen;
// RIGHT enters/selects and UP/DOWN move through items.

// Forward declaration: the icon painters are defined further down (near the
// other menu-drawing helpers), but the main menu's "Bluetooth" item below
// references icon_bt before that point.
static void icon_bt(int x, int y, display_color_t col);

static menu_item_t s_rfid_menu_items[] = {
    {"125kHz Oku",   action_rfid_125khz, NULL, NULL},
    {"13.56MHz Oku", action_nfc_1356mhz, NULL, NULL},
    {"13.56MHz Kopyala", action_rfid_clone, NULL, NULL},
    {"125kHz Kaydet",    action_rfid_save_125khz,  NULL, NULL},
    {"13.56MHz Kaydet",  action_rfid_save_1356mhz, NULL, NULL},
    {"RFID Kütüphanesi",   action_rfid_library,      NULL, NULL},
};

static menu_item_t s_ir_menu_items[] = {
    {"IR Gönderim Testi", action_ir_send_test, NULL, NULL},
    {"IR Öğren",          action_ir_learn,     NULL, NULL},
    {"IR Kütüphanesi",    action_ir_library,   NULL, NULL},
    // (N/A): unavailable on this hardware profile, not a bug -- see the
    // action's own comment and KNOWN_ISSUES.md's Round 27. Marked in the
    // label itself so the menu doesn't present a dead control as a normal
    // one; selecting it still shows the full explanation on-screen.
    {"IR Yön Bul (Yok)", action_ir_direction_find, NULL, NULL},
};

static menu_item_t s_wifi_menu_items[] = {
    {"WiFi Tara/Bağlan", action_wifi_scan_test, NULL, NULL},
    {"WiFi Durum", action_wifi_status, NULL, NULL},
    {"WiFi Ağım (AP)", action_wifi_my_network, NULL, NULL},
};

static menu_item_t s_bluetooth_menu_items[] = {
    {"BT Tara", action_bt_scan, NULL, NULL},
    {"BLE Radar", action_bt_radar, NULL, NULL},
};

static menu_item_t s_security_lab_menu_items[] = {
    {"Güvenli Kullanım", action_security_lab_guide, NULL, NULL},
};

// Note: This menu now contains active packet transmission alongside
// the existing receive-only actions.
static menu_item_t s_hacking_menu_items[] = {
    {"WiFi İzleme (RX)", action_wifi_monitor, NULL, NULL},
    {"Kanal Haritası (RX)", action_wifi_channel_map, NULL, NULL},
    {"Çerçeve İstat (RX)", action_wifi_frame_stats, NULL, NULL},
    {"Probe Yakala (RX)", action_wifi_probe_capture, NULL, NULL},
    {"WiFi Radar (RX)", action_wifi_radar, NULL, NULL},
    {"BLE Keşif (RX)", action_bt_scan, NULL, NULL},
};


// --- Main-menu icons (16x16 cell, drawn with basic primitives) -------------
// Each paints into the cell at (x,y) using `col`. Kept simple and monochrome
// so they read on the small panel and follow the row's selected/normal color.

// All icons draw within a ~12px-tall box (y+0 .. y+12) so that with the +2px
// vertical nudge in menu.c they stay inside the 16px row and never bleed into
// the row above or below. Keep every y-offset in the 0..12 range.

static void icon_rfid(int x, int y, display_color_t col)
{
    // A card outline with a small "wave" (contactless) to its right.
    display_draw_line(x + 1, y + 2, x + 8, y + 2, col);
    display_draw_line(x + 1, y + 11, x + 8, y + 11, col);
    display_draw_line(x + 1, y + 2, x + 1, y + 11, col);
    display_draw_line(x + 8, y + 2, x + 8, y + 11, col);
    display_draw_pixel(x + 11, y + 5, col);
    display_draw_pixel(x + 11, y + 7, col);
    display_draw_pixel(x + 12, y + 6, col);
}

static void icon_ir(int x, int y, display_color_t col)
{
    // A remote-style emitter: a dot with rays fanning out to the right.
    display_fill_circle(x + 3, y + 6, 2, col);
    display_draw_line(x + 6, y + 6, x + 12, y + 6, col);
    display_draw_line(x + 6, y + 3, x + 11, y + 2, col);
    display_draw_line(x + 6, y + 9, x + 11, y + 10, col);
}

// Draws the upper fan of a circle (the arc from about 210deg to 330deg, i.e.
// the "^" cap over the centre) point by point, so WiFi looks like the real
// half-arc signal glyph instead of full rings.
static void icon_wifi_arc(int cx, int cy, int r, display_color_t col)
{
    for (int t = 210; t <= 330; t += 6) {
        float a = (float)t * 0.01745329f; // deg -> rad
        int px = cx + (int)(r * cosf(a) + (cosf(a) >= 0 ? 0.5f : -0.5f));
        int py = cy + (int)(r * sinf(a) + (sinf(a) >= 0 ? 0.5f : -0.5f));
        display_draw_pixel(px, py, col);
    }
}

static void icon_wifi(int x, int y, display_color_t col)
{
    // A base dot with two upward half-arcs -- the classic WiFi signal glyph.
    display_fill_circle(x + 7, y + 11, 1, col);
    icon_wifi_arc(x + 7, y + 11, 4, col);
    icon_wifi_arc(x + 7, y + 11, 8, col);
}

static void icon_bt(int x, int y, display_color_t col)
{
    // Stylized Bluetooth rune, spine from y+1 to y+11.
    int mx = x + 7;
    display_draw_line(mx, y + 1, mx, y + 11, col);
    display_draw_line(mx, y + 1, mx + 3, y + 4, col);
    display_draw_line(mx + 3, y + 4, mx - 3, y + 8, col);
    display_draw_line(mx, y + 11, mx + 3, y + 8, col);
    display_draw_line(mx + 3, y + 8, mx - 3, y + 4, col);
}

static void icon_seclab(int x, int y, display_color_t col)
{
    // A shield outline.
    display_draw_line(x + 2, y + 1, x + 12, y + 1, col);
    display_draw_line(x + 2, y + 1, x + 2, y + 7, col);
    display_draw_line(x + 12, y + 1, x + 12, y + 7, col);
    display_draw_line(x + 2, y + 7, x + 7, y + 12, col);
    display_draw_line(x + 12, y + 7, x + 7, y + 12, col);
}

static void icon_hacking(int x, int y, display_color_t col)
{
    // A skull-ish glyph: rounded top, two eyes, a jaw line.
    display_draw_circle(x + 7, y + 5, 4, col);
    display_draw_pixel(x + 5, y + 5, col);
    display_draw_pixel(x + 9, y + 5, col);
    display_draw_line(x + 5, y + 10, x + 9, y + 10, col);
    display_draw_line(x + 6, y + 10, x + 6, y + 12, col);
    display_draw_line(x + 8, y + 10, x + 8, y + 12, col);
}

static void icon_errors(int x, int y, display_color_t col)
{
    // Warning triangle with an exclamation.
    display_draw_line(x + 7, y + 1, x + 1, y + 12, col);
    display_draw_line(x + 7, y + 1, x + 13, y + 12, col);
    display_draw_line(x + 1, y + 12, x + 13, y + 12, col);
    display_draw_line(x + 7, y + 5, x + 7, y + 8, col);
    display_draw_pixel(x + 7, y + 10, col);
}

static void icon_about(int x, int y, display_color_t col)
{
    // An "i" in a circle.
    display_draw_circle(x + 7, y + 6, 5, col);
    display_draw_pixel(x + 7, y + 3, col);
    display_draw_line(x + 7, y + 5, x + 7, y + 9, col);
}

// A small battery glyph: a 20x9 body with a cap nub, filled from the left in
// proportion to `percent` (0..100). Drawn with its top-left at (x, y). When
// the fuel gauge has no reading (divider unwired), pass percent < 0 to draw an
// empty outline instead of a misleading fill.
static void draw_battery_glyph(int x, int y, int percent, display_color_t col)
{
    const int bw = 20, bh = 9;
    // Outline.
    display_draw_line(x, y, x + bw, y, col);
    display_draw_line(x, y + bh, x + bw, y + bh, col);
    display_draw_line(x, y, x, y + bh, col);
    display_draw_line(x + bw, y, x + bw, y + bh, col);
    // Positive-terminal cap on the right.
    display_fill_rect(x + bw + 1, y + 2, 2, bh - 3, col);

    if (percent < 0) {
        return; // unknown: empty outline only
    }
    if (percent > 100) percent = 100;
    int inner_w = bw - 4;
    int fill = (inner_w * percent) / 100;
    if (fill > 0) {
        display_fill_rect(x + 2, y + 2, fill, bh - 3, col);
    }
}

// Draws the battery badge (glyph + "NN%") right-aligned so its right edge sits
// at x_right, vertically at y. Reads the fuel gauge itself. Colour shifts to
// the error red below 15%. Safe to call from any full-screen redraw.
static void draw_status_battery(int x_right, int y, display_color_t fg)
{
    battery_reading_t bat = battery_read();
    char txt[8];
    display_color_t col = fg;
    if (bat.valid) {
        if (bat.percent <= 15) {
            col = DISPLAY_COLOR_ERROR;
        }
        snprintf(txt, sizeof(txt), "%d%%", bat.percent);
    } else {
        snprintf(txt, sizeof(txt), "--");
    }
    int text_px = (int)strlen(txt) * 8;
    int glyph_w = 23; // body + cap
    int total = glyph_w + 3 + text_px;
    int x = x_right - total;
    if (x < 0) x = 0;
    draw_battery_glyph(x, y, bat.valid ? bat.percent : -1, col);
    display_draw_text_px(x + glyph_w + 3, y - 3, txt, col, display_get_background());
}

// Indices [0..5] below must stay in sync with the menu_link_submenu()
// calls in app_main() -- reordering these items without updating those
// calls (or their hardcoded indices) makes the moved category silently
// do nothing when selected, with no compiler warning.
static menu_item_t s_main_menu_items[] = {
    {"RFID / NFC", NULL, NULL, icon_rfid},
    {"Kızılötesi", NULL, NULL, icon_ir},
    {"WiFi",       NULL, NULL, icon_wifi},
    {"Bluetooth",  NULL, NULL, icon_bt},
    {"SecLab", NULL, NULL, icon_seclab},
    {"Hacking", NULL, NULL, icon_hacking},
    {"Hatalar",    action_error_history, NULL, icon_errors},
    {"Hakkında",   action_about, NULL, icon_about},
};

static menu_t s_main_menu;
static menu_t s_rfid_menu;
static menu_t s_ir_menu;
static menu_t s_wifi_menu;
static menu_t s_bluetooth_menu;
static menu_t s_security_lab_menu;
static menu_t s_hacking_menu;

// Gives each menu its own background tint, then renders it. The main menu
// keeps the default near-black; each category carries its section color into
// its submenu so the color persists while you're inside that section.
static void render_menu_themed(const menu_t *menu)
{
    display_color_t bg = DISPLAY_BG_DEFAULT;
    if (menu == &s_rfid_menu) {
        bg = DISPLAY_BG_RFID;
    } else if (menu == &s_ir_menu) {
        bg = DISPLAY_BG_INFRARED;
    } else if (menu == &s_wifi_menu) {
        bg = DISPLAY_BG_WIFI;
    } else if (menu == &s_bluetooth_menu) {
        bg = DISPLAY_BG_BLUETOOTH;
    } else if (menu == &s_security_lab_menu) {
        bg = DISPLAY_BG_SECURITY;
    } else if (menu == &s_hacking_menu) {
        bg = DISPLAY_BG_HACKING;
    }
    display_set_background(bg);
    menu_render(menu);
    // Overlay the battery badge in the banner's right corner. menu_render()
    // has already flushed; draw on top and flush again (menus are static, so
    // the extra flush is cheap). The banner text is centered, leaving the
    // right corner free. Use the banner's own foreground so it reads on the
    // accent-filled bar.
    if (menu->banner) {
        display_color_t badge_fg = menu->fill_selection ? DISPLAY_COLOR_ACCENT_TEXT
                                                        : menu->selected_text_color;
        draw_status_battery(DISPLAY_WIDTH_PX - 2, 4, badge_fg);
        display_flush();
    }
}

static void render_scan_screen(const char *title)
{
    display_clear();
    display_draw_text_centered(0, title, DISPLAY_COLOR_ACCENT);
    display_draw_text(2, 0, s_last_scan_line[0] ? s_last_scan_line : "Taranıyor...");
    display_draw_text(6, 0, "GERİ: çık");
    display_flush();
}

static void render_ir_direction_screen(uint8_t flags)
{
    display_clear();
    display_draw_text_centered(0, "IR Yön Bulma", DISPLAY_COLOR_ACCENT);
    if (!ir_direction_is_available()) {
        // ir_direction_init() isn't called on this build (see
        // KNOWN_ISSUES.md's Round 13/15) -- say so explicitly rather than
        // leaving the user on a "Waiting for IR..." screen that can never
        // report anything.
        display_draw_text_color(2, 0, "Kullanılamıyor", DISPLAY_COLOR_ERROR);
        display_draw_text(3, 0, "bu sürümde");
        display_draw_text(5, 0, "(RMT kanalları IR RX/TX");
        display_draw_text(6, 0, "tarafından kullanılıyor)");
        display_draw_text(8, 0, "GERİ: çık");
        display_flush();
        return;
    }
    if (s_last_scan_line[0]) {
        display_draw_text(2, 0, s_last_scan_line);
    } else {
        display_draw_text(2, 0, "IR bekleniyor...");
    }
    char dirs[DISPLAY_COLS + 1];
    snprintf(dirs, sizeof(dirs), "%c%c%c%c",
             (flags & IR_DIR_NORTH) ? 'N' : '-',
             (flags & IR_DIR_EAST)  ? 'E' : '-',
             (flags & IR_DIR_SOUTH) ? 'S' : '-',
             (flags & IR_DIR_WEST)  ? 'W' : '-');
    display_draw_text(4, 0, dirs);
    display_draw_text(6, 0, "GERİ: çık");
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
    buttons_init();
    show_boot_splash();
    show_startup_notice();
    ir_driver_init();
    // The four-receiver direction finder is retained for a future hardware
    // profile but is not initialized on this fixed C6-Pico pin map.
    if (BOARD_HAS_RC522) {
        rc522_init();
    }
    rdm6300_init();
    vibration_init();
    battery_init();
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
    menu_init(&s_security_lab_menu, s_security_lab_menu_items,
              sizeof(s_security_lab_menu_items) / sizeof(s_security_lab_menu_items[0]));
    menu_init(&s_hacking_menu, s_hacking_menu_items,
              sizeof(s_hacking_menu_items) / sizeof(s_hacking_menu_items[0]));

    menu_set_banner(&s_main_menu, "Makeshift Flipper");
    menu_set_banner(&s_rfid_menu, "RFID / NFC");
    menu_set_banner(&s_ir_menu, "Kızılötesi");
    menu_set_banner(&s_wifi_menu, "WiFi");
    menu_set_banner(&s_bluetooth_menu, "Bluetooth");
    menu_set_banner(&s_security_lab_menu, "SecLab");
    menu_set_banner(&s_hacking_menu, "HACKING");
    menu_set_background(&s_hacking_menu, DISPLAY_BG_HACKING);
    menu_set_accent(&s_hacking_menu, DISPLAY_COLOR_HACKING_ACCENT);
    menu_set_text_style(&s_hacking_menu, DISPLAY_COLOR_HACKING_TEXT,
                        DISPLAY_COLOR_HACKING_SELECTED, false);

    menu_link_submenu(&s_main_menu, &s_main_menu_items[0], &s_rfid_menu);
    menu_link_submenu(&s_main_menu, &s_main_menu_items[1], &s_ir_menu);
    menu_link_submenu(&s_main_menu, &s_main_menu_items[2], &s_wifi_menu);
    menu_link_submenu(&s_main_menu, &s_main_menu_items[3], &s_bluetooth_menu);
    menu_link_submenu(&s_main_menu, &s_main_menu_items[4], &s_security_lab_menu);
    menu_link_submenu(&s_main_menu, &s_main_menu_items[5], &s_hacking_menu);

    // Catches a forgotten/misindexed menu_link_submenu() call above at
    // boot (see KNOWN_ISSUES.md) instead of leaving a menu item that
    // silently does nothing when a user eventually selects it. Only
    // s_main_menu has category items (NULL on_select, wired to a
    // submenu) -- the rest are flat leaf-item menus, safe by construction,
    // so they don't need this check.
    menu_assert_fully_wired(&s_main_menu);

    s_active_menu = &s_main_menu;
    render_menu_themed(s_active_menu);

    while (1) {
        button_id_t event = buttons_poll();

        if (s_screen == APP_SCREEN_MENU) {
            bool needs_render = false;
            if (event != BUTTON_COUNT) {
                menu_t *next = menu_handle_button(s_active_menu, event);
                if (next != s_active_menu) {
                    s_active_menu = next;
                    // One-time red scope reminder when entering Hacking.
                    if (next == &s_hacking_menu) {
                        show_hacking_intro();
                    }
                }
                needs_render = true;
            }
            if (s_active_menu->anim_offset_px != 0) {
                menu_animate_tick(s_active_menu);
                needs_render = true;
            }
            if (needs_render) {
                render_menu_themed(s_active_menu);
            }
        } else if (event == BUTTON_BACK) {
            if (s_screen == APP_SCREEN_SCAN_1356MHZ) {
                rc522_antenna_off();
            }
            s_screen = APP_SCREEN_MENU;
            render_menu_themed(s_active_menu);
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
                             "7/10 bayt UID: yok");
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
