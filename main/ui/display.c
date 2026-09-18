#include "display.h"

#include <stdbool.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"
#include "font8x8_basic.h"

// Matches the wiring diagram: OLED SDA=GPIO7, SCL=GPIO8.
#define I2C_SDA_GPIO 7
#define I2C_SCL_GPIO 8
#define I2C_PORT     I2C_NUM_0
#define OLED_ADDR    0x3C

#define PANEL_WIDTH  128
#define PANEL_HEIGHT 64

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel = NULL;
static uint8_t s_framebuf[PANEL_WIDTH * PANEL_HEIGHT / 8];

void display_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = OLED_ADDR,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };
    esp_lcd_panel_io_handle_t io_handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus_handle, &io_cfg, &io_handle));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    display_clear();
    display_flush();
    ESP_LOGI(TAG, "OLED initialized");
}

void display_clear(void)
{
    memset(s_framebuf, 0, sizeof(s_framebuf));
}

static inline void set_pixel(int x, int y, bool invert)
{
    if (x < 0 || x >= PANEL_WIDTH || y < 0 || y >= PANEL_HEIGHT) {
        return;
    }
    int byte_idx = (y / 8) * PANEL_WIDTH + x;
    uint8_t mask = 1 << (y % 8);
    if (invert) {
        s_framebuf[byte_idx] ^= mask;
    } else {
        s_framebuf[byte_idx] |= mask;
    }
}

void display_draw_text_px(int x0, int y0, const char *text, bool invert)
{
    for (int i = 0; text[i] != '\0'; i++) {
        const uint8_t *glyph = font8x8_basic[(uint8_t)text[i]];
        int cx0 = x0 + i * 8;

        if (cx0 >= PANEL_WIDTH || cx0 + 8 <= 0) {
            continue; // fully off-screen horizontally
        }

        for (int gy = 0; gy < 8; gy++) {
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < 8; gx++) {
                if (!(bits & (1 << gx))) {
                    continue;
                }
                set_pixel(cx0 + gx, y0 + gy, invert);
            }
        }
    }
}

void display_draw_text(int row, int col, const char *text)
{
    if (row < 0 || row >= DISPLAY_ROWS || col < 0) {
        return;
    }
    int n = DISPLAY_COLS - col;
    if (n <= 0) {
        return;
    }
    // display_draw_text_px has no column truncation, so it's the caller's
    // job here; callers within this codebase always pass short-enough
    // strings, but guard anyway to keep this entry point's documented
    // "truncates at DISPLAY_COLS" contract honest.
    char clipped[DISPLAY_COLS + 1];
    int i = 0;
    for (; text[i] != '\0' && i < n && i < DISPLAY_COLS; i++) {
        clipped[i] = text[i];
    }
    clipped[i] = '\0';

    display_draw_text_px(col * 8, row * 8, clipped, false);
}

void display_fill_rect(int x, int y, int w, int h)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            set_pixel(xx, yy, false);
        }
    }
}

void display_flush(void)
{
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, s_framebuf);
}
