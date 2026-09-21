#include "display.h"

#include <stdbool.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"
#include "font8x16_basic.h"

// Waveshare Pico-LCD-1.3 fixed pinout (module HAT, cannot move -- see the
// migration plan's pin budget table / README's pin plan). SCK/MOSI sit on
// spi1's default hardware-muxed pin group; CS/DC/RST/BL are plain
// bit-banged GPIO (not native SPI-function pins on RP2040), same as on the
// P4 build.
//
// NOTE: the exact ST7789 init command sequence below (gamma/porch/MADCTL/
// COLMOD register writes) is the standard sequence used across ST7789V/
// ST7789VW community and vendor ports (including Waveshare's own C demos
// for this same display family). Cross-check it against Waveshare's
// actual Pico-LCD-1.3 demo source before first flash, per the migration
// plan's Phase 1 action item -- this file was written without access to
// that demo in this environment.
#define LCD_SCK_GPIO  10
#define LCD_MOSI_GPIO 11
#define LCD_CS_GPIO   9
#define LCD_DC_GPIO   8
#define LCD_RST_GPIO  12
#define LCD_BL_GPIO   13

#define LCD_SPI_PORT     spi1
#define LCD_SPI_CLOCK_HZ (40 * 1000 * 1000) // 40MHz, within the ST7789's rated SPI clock

#define PANEL_WIDTH  DISPLAY_WIDTH_PX
#define PANEL_HEIGHT DISPLAY_HEIGHT_PX

// ST7789 command bytes used during init/flush.
#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT  0x11
#define ST7789_INVON   0x21
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_MADCTL  0x36
#define ST7789_COLMOD  0x3A

static bool s_panel_ready = false;
// RGB565, one uint16_t per pixel -- 240*240*2 = 115200 bytes. Comfortably
// within the Pico's 264KB SRAM.
static uint16_t s_framebuf[PANEL_WIDTH * PANEL_HEIGHT];

static inline void lcd_cs_select(void)   { gpio_put(LCD_CS_GPIO, 0); }
static inline void lcd_cs_deselect(void) { gpio_put(LCD_CS_GPIO, 1); }

static void lcd_write_cmd(uint8_t cmd)
{
    gpio_put(LCD_DC_GPIO, 0); // command
    lcd_cs_select();
    spi_write_blocking(LCD_SPI_PORT, &cmd, 1);
    lcd_cs_deselect();
}

static void lcd_write_data(const uint8_t *data, size_t len)
{
    gpio_put(LCD_DC_GPIO, 1); // data
    lcd_cs_select();
    spi_write_blocking(LCD_SPI_PORT, data, len);
    lcd_cs_deselect();
}

static void lcd_write_data_byte(uint8_t b)
{
    lcd_write_data(&b, 1);
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4] = { (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
                          (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF) };
    uint8_t raset[4] = { (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
                          (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF) };

    lcd_write_cmd(ST7789_CASET);
    lcd_write_data(caset, sizeof(caset));
    lcd_write_cmd(ST7789_RASET);
    lcd_write_data(raset, sizeof(raset));
    lcd_write_cmd(ST7789_RAMWR);
}

void display_init(void)
{
    // Backlight GPIO.
    gpio_init(LCD_BL_GPIO);
    gpio_set_dir(LCD_BL_GPIO, GPIO_OUT);
    gpio_put(LCD_BL_GPIO, 1); // backlight on

    // CS/DC/RST as plain bit-banged GPIO.
    gpio_init(LCD_CS_GPIO);
    gpio_set_dir(LCD_CS_GPIO, GPIO_OUT);
    gpio_put(LCD_CS_GPIO, 1);

    gpio_init(LCD_DC_GPIO);
    gpio_set_dir(LCD_DC_GPIO, GPIO_OUT);

    gpio_init(LCD_RST_GPIO);
    gpio_set_dir(LCD_RST_GPIO, GPIO_OUT);
    gpio_put(LCD_RST_GPIO, 1);

    // SPI peripheral + hardware-muxed SCK/MOSI pins (write-only panel, no
    // MISO line used).
    spi_init(LCD_SPI_PORT, LCD_SPI_CLOCK_HZ);
    spi_set_format(LCD_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(LCD_SCK_GPIO, GPIO_FUNC_SPI);
    gpio_set_function(LCD_MOSI_GPIO, GPIO_FUNC_SPI);

    // Hardware reset pulse.
    gpio_put(LCD_RST_GPIO, 1);
    sleep_ms(10);
    gpio_put(LCD_RST_GPIO, 0);
    sleep_ms(10);
    gpio_put(LCD_RST_GPIO, 1);
    sleep_ms(120);

    lcd_write_cmd(ST7789_SWRESET);
    sleep_ms(150);

    lcd_write_cmd(ST7789_SLPOUT);
    sleep_ms(120);

    // Interface pixel format: 16bpp (RGB565).
    lcd_write_cmd(ST7789_COLMOD);
    lcd_write_data_byte(0x55);
    sleep_ms(10);

    // Memory access control: RGB order, no mirror/rotate -- matches this
    // codebase's row/col orientation assumptions (unchanged from the P4
    // build's esp_lcd defaults). Adjust if Waveshare's demo uses a
    // different MADCTL value for correct orientation on this module.
    lcd_write_cmd(ST7789_MADCTL);
    lcd_write_data_byte(0x00);

    // Most ST7789 modules (including 1.3" variants) need inversion on to
    // show true colors -- same as the P4 build's esp_lcd_panel_invert_color(true).
    lcd_write_cmd(ST7789_INVON);
    sleep_ms(10);

    lcd_write_cmd(ST7789_DISPON);
    sleep_ms(20);

    s_panel_ready = true;

    display_clear();
    display_flush();
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
    if (!s_panel_ready) {
        return;
    }
    lcd_set_window(0, 0, PANEL_WIDTH - 1, PANEL_HEIGHT - 1);
    // Whole-framebuffer burst write, matching esp_lcd_panel_draw_bitmap's
    // single-call semantics on the P4 build. s_framebuf is already
    // RGB565 big-endian-on-the-wire compatible with the ST7789's expected
    // byte order (same layout the P4 build sent via esp_lcd).
    lcd_write_data((const uint8_t *)s_framebuf, sizeof(s_framebuf));
}
