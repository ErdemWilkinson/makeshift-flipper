#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "input/buttons.h"
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
} app_screen_t;

static app_screen_t s_screen = APP_SCREEN_MENU;
static bool s_screen_dirty = true; // forces a render on the next loop tick
static menu_t *s_main_menu_ref; // set in app_main(), used to redraw after a blocking action

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

// Sends a fixed test frame. Replace with a real "pick a saved code" screen
// once there's a way to store/browse captured IR codes.
static void action_ir_send_test(void)
{
    ir_nec_frame_t frame = { .address = 0x00, .command = 0x45 };
    ESP_LOGI(TAG, "IR TX test frame: addr=0x%02X cmd=0x%02X", frame.address, frame.command);
    ir_driver_send(&frame);
}

// Blocks for a few seconds while the C6 scans. Replace with a real
// "browse networks" screen once there's a UI for picking one to connect to.
static void action_wifi_scan_test(void)
{
    c6_network_t networks[C6_MAX_NETWORKS];
    int count = c6_link_scan(networks, C6_MAX_NETWORKS);
    if (count < 0) {
        ESP_LOGW(TAG, "Wi-Fi scan failed (C6 not responding?)");
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

    menu_render(s_main_menu_ref);
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
        menu_render(s_main_menu_ref);
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
        menu_render(s_main_menu_ref);
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
        menu_render(s_main_menu_ref);
        return;
    }

    display_clear();
    display_draw_text(0, 0, "Connecting...");
    display_flush();

    bool ok = c6_link_connect(networks[selected].ssid, entry.buffer);

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

    menu_render(s_main_menu_ref);
}

static void action_about(void)
{
    ESP_LOGI(TAG, "Makeshift Flipper - skeleton build");
}

static const menu_item_t s_main_menu_items[] = {
    {"Read 125kHz",   action_rfid_125khz},
    {"Read 13.56MHz", action_nfc_1356mhz},
    {"IR Send Test",  action_ir_send_test},
    {"WiFi Scan Test", action_wifi_scan_test},
    {"WiFi Setup",    action_wifi_setup},
    {"WiFi Setup Manual", action_wifi_setup_manual},
    {"About",         action_about},
};

static void render_scan_screen(const char *title)
{
    display_clear();
    display_draw_text(0, 0, title);
    display_draw_text(2, 0, s_last_scan_line[0] ? s_last_scan_line : "Scanning...");
    display_draw_text(6, 0, "BACK button: exit");
    display_flush();
}

void app_main(void)
{
    display_init();
    buttons_init();
    ir_driver_init();
    rc522_init();
    rdm6300_init();
    vibration_init();
    c6_link_init();

    menu_t main_menu;
    menu_init(&main_menu, s_main_menu_items,
              sizeof(s_main_menu_items) / sizeof(s_main_menu_items[0]));
    s_main_menu_ref = &main_menu;

    menu_render(&main_menu);

    while (1) {
        button_id_t event = buttons_poll();

        if (s_screen == APP_SCREEN_MENU) {
            bool needs_render = false;
            if (event != BUTTON_COUNT) {
                menu_handle_button(&main_menu, event);
                needs_render = true;
            }
            if (main_menu.anim_offset_px != 0) {
                menu_animate_tick(&main_menu);
                needs_render = true;
            }
            if (needs_render) {
                menu_render(&main_menu);
            }
        } else if (event == BUTTON_BACK) {
            if (s_screen == APP_SCREEN_SCAN_1356MHZ) {
                rc522_antenna_off();
            }
            s_screen = APP_SCREEN_MENU;
            menu_render(&main_menu);
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
