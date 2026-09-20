#include "display.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "font8x16_basic.h"

// SPI pins for the ST7789 LCD, on their own bus (SPI3_HOST) separate from
// the RC522's SPI2_HOST (GPIO9-13) -- no bus sharing, no chip-select
// contention between the two SPI peripherals. GPIO7/8 are free now that
// the OLED's I2C wiring is gone; GPIO14/15 are free because
// ir_direction_init() is not called on this build (see KNOWN_ISSUES.md
// Round 13) -- if that ever changes, this panel's CS/DC need to move, since
// GPIO14/15 are ir_direction.c's GPIO_SOUTH/GPIO_WEST.
#define LCD_SCK_GPIO  7
#define LCD_MOSI_GPIO 8
#define LCD_CS_GPIO   14
#define LCD_DC_GPIO   15
#define LCD_RST_GPIO  6
#define LCD_BL_GPIO   21 // backlight enable; tie to 3V3 instead if your module has no BL pin

#define LCD_SPI_HOST     SPI3_HOST
#define LCD_SPI_CLOCK_HZ (40 * 1000 * 1000) // 40MHz, within the ST7789's rated SPI clock

#define PANEL_WIDTH  DISPLAY_WIDTH_PX
#define PANEL_HEIGHT DISPLAY_HEIGHT_PX

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel = NULL;
// RGB565, one uint16_t per pixel -- 240*240*2 = 115200 bytes. Comfortably
// within the P4's internal RAM; if that ever changes, this is the first
// thing to move to PSRAM (heap_caps_malloc(..., MALLOC_CAP_SPIRAM)).
static uint16_t s_framebuf[PANEL_WIDTH * PANEL_HEIGHT];

void display_init(void)
{
    gpio_config_t bl_cfg = {
        .pin_bit_mask = (1ULL << LCD_BL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_BL_GPIO, 1); // backlight on

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_SCK_GPIO,
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = -1, // write-only panel, no MISO line used
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PANEL_WIDTH * PANEL_HEIGHT * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        // A failed bus init (e.g. pins already claimed) must not wedge the
        // whole device. Drawing still updates the in-memory framebuffer;
        // flushing is simply a no-op until a panel is attached.
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return;
    }

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_DC_GPIO,
        .cs_gpio_num = LCD_CS_GPIO,
        .pclk_hz = LCD_SPI_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t io_handle;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI panel IO init failed: %s", esp_err_to_name(err));
        return;
    }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ST7789 panel init failed: %s", esp_err_to_name(err));
        s_panel = NULL;
        return;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true)); // most 1.8" ST7789 modules need this to show true colors
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    display_clear();
    display_flush();
    ESP_LOGI(TAG, "ST7789 LCD initialized (SCK=%d MOSI=%d CS=%d DC=%d RST=%d)",
             LCD_SCK_GPIO, LCD_MOSI_GPIO, LCD_CS_GPIO, LCD_DC_GPIO, LCD_RST_GPIO);
}

void display_clear(void)
{
    for (int i = 0; i < PANEL_WIDTH * PANEL_HEIGHT; i++) {
        s_framebuf[i] = DISPLAY_COLOR_BACKGROUND;
    }
}

static inline void set_pixel(int x, int y, display_color_t color)
{
    if (x < 0 || x >= PANEL_WIDTH || y < 0 || y >= PANEL_HEIGHT) {
        return;
    }
    s_framebuf[y * PANEL_WIDTH + x] = color;
}

void display_draw_text_px(int x0, int y0, const char *text, display_color_t fg, display_color_t bg)
{
    for (int i = 0; text[i] != '\0'; i++) {
        const uint8_t *glyph = font8x16_basic[(uint8_t)text[i]];
        int cx0 = x0 + i * 8;

        if (cx0 >= PANEL_WIDTH || cx0 + 8 <= 0) {
            continue; // fully off-screen horizontally
        }

        for (int gy = 0; gy < 16; gy++) {
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < 8; gx++) {
                bool on = (bits & (1 << gx)) != 0;
                set_pixel(cx0 + gx, y0 + gy, on ? fg : bg);
            }
        }
    }
}

void display_draw_text_color(int row, int col, const char *text, display_color_t color)
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

    display_draw_text_px(col * 8, row * 16, clipped, color, DISPLAY_COLOR_BACKGROUND);
}

void display_draw_text(int row, int col, const char *text)
{
    display_draw_text_color(row, col, text, DISPLAY_COLOR_TEXT);
}

void display_fill_rect(int x, int y, int w, int h, display_color_t color)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            set_pixel(xx, yy, color);
        }
    }
}

void display_flush(void)
{
    if (s_panel == NULL) {
        return;
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, s_framebuf);
}
