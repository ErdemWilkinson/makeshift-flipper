#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "diag/diag.h"
#include "input/buttons.h"
#include "ir/ir_direction.h"
#include "ir/ir_driver.h"
#include "net/c6_link.h"
#include "rfid/rc522.h"
#include "rfid/rdm6300.h"
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
        display_draw_text(0, 0, "RFID Clone");
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
static bool show_dump_and_confirm(const rc522_card_dump_t *dump)
{
    int sector = 0;
    for (;;) {
        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "Sector %d/%d %s", sector, RC522_SECTOR_COUNT - 1,
                  dump->sectors[sector].readable ? "" : "(locked)");
        display_draw_text(0, 0, header);

        if (dump->sectors[sector].readable) {
            for (int b = 0; b < RC522_BLOCKS_PER_SECTOR; b++) {
                char line[DISPLAY_COLS + 1];
                int n = 0;
                const uint8_t *block = dump->sectors[sector].blocks[b];
                for (int i = 0; i < 8 && n < DISPLAY_COLS - 2; i++) { // first 8 bytes/row fits 21 cols
                    n += snprintf(&line[n], sizeof(line) - n, "%02X", block[i]);
                }
                display_draw_text(1 + b, 0, line);
            }
        } else {
            display_draw_text(2, 0, "No default key worked");
        }

        char footer[DISPLAY_COLS + 1];
        snprintf(footer, sizeof(footer), "%d/%d sectors read", dump->sectors_read, RC522_SECTOR_COUNT);
        display_draw_text(6, 0, footer);
        display_draw_text(7, 0, "PRESS:clone BACK:exit");
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

    rc522_card_dump_t *dump = malloc(sizeof(rc522_card_dump_t));
    if (dump == NULL) {
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
    display_draw_text(0, 0, "RFID Clone");
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
    display_draw_text(0, 0, "RFID Clone");
    display_draw_text(2, 0, "Writing sectors...");
    display_flush();
    int written = clone_to_card(&target_uid, dump, /* write_trailers = */ false);
    if (written == 0) {
        diag_record_error("RFID Clone", "RC522_CLONE_WRITE_FAILED");
    }

    display_clear();
    display_draw_text(0, 0, "RFID Clone");
    char result_line[DISPLAY_COLS + 1];
    snprintf(result_line, sizeof(result_line), "%d/%d sectors cloned", written, dump->sectors_read);
    display_draw_text(2, 0, result_line);
    display_draw_text(3, 0, is_magic ? "UID cloned (gen1a)" : "UID not cloned");
    display_draw_text(5, 0, "Trailers not written");
    display_draw_text(6, 0, "(data blocks only)");
    display_draw_text(7, 0, "Press any key");
    display_flush();

    button_id_t any;
    do {
        any = buttons_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (any == BUTTON_COUNT);

    free(dump);
    rc522_antenna_off();
    menu_render(s_active_menu);
}

// Sends a fixed test frame. Replace with a real "pick a saved code" screen
// once there's a way to store/browse captured IR codes.
static void action_ir_send_test(void)
{
    ir_nec_frame_t frame = { .address = 0x00, .command = 0x45 };
    ESP_LOGI(TAG, "IR TX test frame: addr=0x%02X cmd=0x%02X", frame.address, frame.command);
    ir_driver_send(&frame);
}

// Opens the 4-receiver direction-finding screen. Requires the
// ir_direction.c hardware (4x VS1838B on GPIO5/6/14/15 by default) to be
// wired up -- see that file's header comment. Untested on real hardware,
// same as everything else in this repo (see README).
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
// The AP password shown here must match AP_PASSWORD in
// c6-firmware/main/wifi_setup_ap.c -- they're two separate firmware builds
// with no shared header, so this is a manual sync point if it's ever changed.
static void action_wifi_setup(void)
{
    display_clear();
    display_draw_text(0, 0, "WiFi Setup");
    display_draw_text(1, 0, "AP: MakeshiftFlip");
    display_draw_text(2, 0, "per-Setup");
    display_draw_text(3, 0, "Pwd: flipper123");
    display_draw_text(5, 0, "Then open 192.168");
    display_draw_text(6, 0, ".4.1 in browser");
    display_flush();

    bool ok = c6_link_setup();
    if (!ok) {
        diag_record_error("WiFi Setup", "C6_LINK_SETUP_FAILED");
    }

    display_clear();
    display_draw_text(0, 0, "WiFi Setup");
    display_draw_text(2, 0, ok ? "Connected!" : "Failed / timed out");
    display_draw_text(6, 0, "Press any key");
    display_flush();

    button_id_t any;
    do {
        any = buttons_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (any == BUTTON_COUNT);

    menu_render(s_active_menu);
}

// No-phone fallback: scan, pick a network with UP/DOWN/PRESS, then type
// the password on the joystick-driven scroll keyboard. Fully blocking,
// same as the other setup actions -- there's no other screen to interrupt
// this with anyway.
static void action_wifi_setup_manual(void)
{
    display_clear();
    display_draw_text(0, 0, "Scanning...");
    display_flush();

    c6_network_t networks[C6_MAX_NETWORKS];
    int count = c6_link_scan(networks, C6_MAX_NETWORKS);
    if (count <= 0) {
        display_clear();
        display_draw_text(0, 0, "No networks found");
        display_draw_text(2, 0, "Press any key");
        display_flush();
        button_id_t any;
        do {
            any = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (any == BUTTON_COUNT);
        menu_render(s_active_menu);
        return;
    }

    // --- Network picker: UP/DOWN moves, PRESS selects, LEFT cancels. ---
    int selected = 0;
    bool picked = false;
    bool cancelled = false;
    for (;;) {
        display_clear();
        display_draw_text(0, 0, "Pick a network:");
        for (int i = 0; i < count && i < DISPLAY_ROWS - 1; i++) {
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "%c%s", (i == selected) ? '>' : ' ', networks[i].ssid);
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
    snprintf(title, sizeof(title), "Pwd for %s", networks[selected].ssid);

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
    display_draw_text(0, 0, "Connecting...");
    display_flush();

    bool ok = c6_link_connect(networks[selected].ssid, entry.buffer);
    if (!ok) {
        diag_record_error("WiFi Setup Manual", "C6_LINK_CONNECT_FAILED");
    }

    display_clear();
    display_draw_text(0, 0, "WiFi Setup");
    display_draw_text(2, 0, ok ? "Connected!" : "Failed");
    display_draw_text(6, 0, "Press any key");
    display_flush();

    button_id_t any;
    do {
        any = buttons_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (any == BUTTON_COUNT);

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
    display_draw_text(0, 0, "WiFi Monitor");
    display_draw_text(2, 0, "Starting...");
    display_flush();

    if (!c6_link_monitor_start()) {
        diag_record_error("WiFi Monitor", "C6_LINK_MONITOR_START_FAILED");
        display_clear();
        display_draw_text(0, 0, "WiFi Monitor");
        display_draw_text(2, 0, "Failed to start");
        display_draw_text(6, 0, "Press any key");
        display_flush();
        button_id_t any;
        do {
            any = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (any == BUTTON_COUNT);
        menu_render(s_active_menu);
        return;
    }

    c6_monitor_ap_t aps[C6_MONITOR_MAX_APS];
    for (;;) {
        int count = c6_link_monitor_poll(aps, C6_MONITOR_MAX_APS);

        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "WiFi Monitor (%d)", count);
        display_draw_text(0, 0, header);
        for (int i = 0; i < count && i < DISPLAY_ROWS - 2; i++) {
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "%.12s c%d %ddBm",
                     aps[i].ssid[0] ? aps[i].ssid : "(hidden)", aps[i].channel, aps[i].rssi);
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
    display_draw_text(0, 0, "BT Scan");
    display_draw_text(2, 0, "Starting...");
    display_flush();

    if (!c6_link_bt_scan_start()) {
        diag_record_error("BT Scan", "C6_LINK_BT_SCAN_START_FAILED");
        display_clear();
        display_draw_text(0, 0, "BT Scan");
        display_draw_text(2, 0, "Failed to start");
        display_draw_text(6, 0, "Press any key");
        display_flush();
        button_id_t any;
        do {
            any = buttons_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (any == BUTTON_COUNT);
        menu_render(s_active_menu);
        return;
    }

    c6_bt_device_t devices[C6_BT_MAX_DEVICES];
    for (;;) {
        int count = c6_link_bt_scan_poll(devices, C6_BT_MAX_DEVICES);

        display_clear();
        char header[DISPLAY_COLS + 1];
        snprintf(header, sizeof(header), "BT Scan (%d)", count);
        display_draw_text(0, 0, header);
        for (int i = 0; i < count && i < DISPLAY_ROWS - 2; i++) {
            char line[DISPLAY_COLS + 1];
            snprintf(line, sizeof(line), "%.14s %ddBm",
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
    ESP_LOGI(TAG, "Makeshift Flipper - skeleton build");
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
    display_draw_text(0, 0, "Errors");
    display_draw_text(2, 0, "Sending...");
    display_flush();

    bool ok = c6_link_send_error_log(entries, count);

    display_clear();
    display_draw_text(0, 0, "Errors");
    display_draw_text(2, 0, ok ? "Sent!" : "Send failed");
    if (!ok) {
        display_draw_text(3, 0, "Check WiFi / log");
        display_draw_text(4, 0, "server config");
    }
    display_draw_text(6, 0, "Press any key");
    display_flush();

    button_id_t any;
    do {
        any = buttons_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (any == BUTTON_COUNT);
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
        display_draw_text(0, 0, header);

        if (count == 0) {
            display_draw_text(2, 0, "No errors recorded");
        } else {
            for (int i = 0; i < count - top && i < DISPLAY_ROWS - 2; i++) {
                const diag_entry_t *e = &entries[top + i];
                char ago[16];
                format_relative_time(ago, sizeof(ago), e->timestamp_us);
                char line[DISPLAY_COLS + 1];
                snprintf(line, sizeof(line), "%.9s %.7s %s", e->module, e->code, ago);
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
};

static menu_item_t s_ir_menu_items[] = {
    {"IR Send Test",     action_ir_send_test,      NULL},
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
    display_draw_text(0, 0, title);
    display_draw_text(2, 0, s_last_scan_line[0] ? s_last_scan_line : "Scanning...");
    display_draw_text(6, 0, "BACK button: exit");
    display_flush();
}

static void render_ir_direction_screen(uint8_t flags)
{
    display_clear();
    display_draw_text(0, 0, "IR Direction Find");
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
    display_init();
    buttons_init();
    ir_driver_init();
    ir_direction_init();
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
                             "7/10-byte UID: no support");
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
