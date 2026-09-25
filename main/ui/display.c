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
#include "hardware_profile.h"

// C6-standalone: the C6 has only one general-purpose SPI host (SPI2 -- SPI0/1
// serve the flash), so the ST7789 LCD and the RC522 SHARE this one bus. This
// module owns the shared bus: display_init() calls spi_bus_initialize() once.
// The screen is write-only, so MISO is enabled only when the RC522 hardware
// profile is selected. display_init() must run before rc522_init(). See
// MCU_ARCHITECTURE_DECISION.md and RC522_SHARED_SPI_HOST in rc522.c.
#define LCD_SCK_GPIO  18 // Pico GP10; shared with RC522
#define LCD_MOSI_GPIO 19 // Pico GP11; shared with RC522
#define LCD_SHARED_MISO_GPIO 6 // Pico GP4; RC522 only
#define LCD_CS_GPIO   9  // Pico GP9
#define LCD_DC_GPIO   8  // Pico GP8 (also feeds the onboard RGB LED input)
#define LCD_RST_GPIO  20 // Pico GP12
#define LCD_BL_GPIO   21 // Pico GP13

#define LCD_SPI_HOST     SPI2_HOST
#define LCD_SPI_CLOCK_HZ (40 * 1000 * 1000) // 40MHz, within the ST7789's rated SPI clock

#define PANEL_WIDTH  DISPLAY_WIDTH_PX
#define PANEL_HEIGHT DISPLAY_HEIGHT_PX

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel = NULL;
// RGB565, one uint16_t per pixel -- 240*240*2 = 115200 bytes. Comfortably
// within the C6's internal SRAM, but it is the largest static allocation
// in this build and must be included in the Stage-2 Wi-Fi/BLE RAM budget.
static uint16_t s_framebuf[PANEL_WIDTH * PANEL_HEIGHT];

void display_init(void)
{
    if (LCD_BL_GPIO >= 0) {
        gpio_config_t bl_cfg = {
            .pin_bit_mask = (1ULL << LCD_BL_GPIO),
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&bl_cfg);
        gpio_set_level(LCD_BL_GPIO, 1); // backlight on
    }
    // GPIO6 is joystick UP on the current screen-only build. RC522 builds
    // require this MISO line and a different physical UP connection.
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_SCK_GPIO,
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = BOARD_HAS_RC522 ? LCD_SHARED_MISO_GPIO : -1,
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
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        // The framebuffer stores uint16_t RGB565 pixels on a little-endian
        // C6. SPI sends those bytes as-is; match the ST7789 RAM byte order so
        // red values do not appear green on the panel.
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
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
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    // The device is held rotated 90 degrees clockwise from the panel's native
    // portrait orientation, so swap X/Y and mirror one axis to land the UI
    // upright in landscape. swap_xy alone transposes; the mirror picks which
    // of the two 90-degree directions (flip the mirror axis if it comes out
    // upside-down or mirror-imaged).
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    display_clear();
    display_flush();
    ESP_LOGI(TAG, "ST7789 LCD initialized (SCK=%d MOSI=%d CS=%d DC=%d RST=%d)",
             LCD_SCK_GPIO, LCD_MOSI_GPIO, LCD_CS_GPIO, LCD_DC_GPIO, LCD_RST_GPIO);
}

// The active background color. Screens can retint the whole UI by setting
// this before drawing; display_clear() and the text helpers read it so text
// cells blend into whatever background is current. Defaults to the theme's
// near-black so anything that never calls the setter looks unchanged.
static display_color_t s_active_bg = DISPLAY_COLOR_BACKGROUND;

void display_set_background(display_color_t color)
{
    s_active_bg = color;
}

display_color_t display_get_background(void)
{
    return s_active_bg;
}

void display_clear(void)
{
    for (int i = 0; i < PANEL_WIDTH * PANEL_HEIGHT; i++) {
        s_framebuf[i] = s_active_bg;
    }
}

static inline void set_pixel(int x, int y, display_color_t color)
{
    if (x < 0 || x >= PANEL_WIDTH || y < 0 || y >= PANEL_HEIGHT) {
        return;
    }
    s_framebuf[y * PANEL_WIDTH + x] = color;
}

// The bundled font is ASCII-only. Decode a UTF-8 character before indexing
// it; malformed or unsupported input is rendered as '?' instead of reading
// beyond font8x16_basic[128]. Turkish letters are drawn from ASCII bases.
static uint32_t next_character(const char **cursor)
{
    const unsigned char *p = (const unsigned char *)*cursor;
    uint32_t cp = *p++;
    if (cp >= 0xC2 && cp <= 0xDF && *p >= 0x80 && *p <= 0xBF) {
        cp = ((cp & 0x1F) << 6) | (*p++ & 0x3F);
    } else if (cp >= 0x80) {
        cp = '?';
    }
    *cursor = (const char *)p;
    return cp;
}

static const uint8_t *character_glyph(uint32_t cp, uint8_t composed[16])
{
    char base = 0;
    enum { NONE, CEDILLA, BREVE, UMLAUT, DOT, DOTLESS } mark = NONE;
    switch (cp) {
        case 0x00C7: base = 'C'; mark = CEDILLA; break;
        case 0x00E7: base = 'c'; mark = CEDILLA; break;
        case 0x011E: base = 'G'; mark = BREVE; break;
        case 0x011F: base = 'g'; mark = BREVE; break;
        case 0x0130: base = 'I'; mark = DOT; break;
        case 0x0131: base = 'i'; mark = DOTLESS; break;
        case 0x00D6: base = 'O'; mark = UMLAUT; break;
        case 0x00F6: base = 'o'; mark = UMLAUT; break;
        case 0x015E: base = 'S'; mark = CEDILLA; break;
        case 0x015F: base = 's'; mark = CEDILLA; break;
        case 0x00DC: base = 'U'; mark = UMLAUT; break;
        case 0x00FC: base = 'u'; mark = UMLAUT; break;
        default: return font8x16_basic[cp < 128 ? cp : '?'];
    }

    const uint8_t *source = font8x16_basic[(unsigned char)base];
    bool uppercase = base >= 'A' && base <= 'Z';
    memset(composed, 0, 16);
    if (uppercase && mark != CEDILLA) {
        // Uppercase ASCII occupies rows 0..13; make room for the accent.
        memcpy(composed + 2, source, 14);
    } else {
        memcpy(composed, source, 16);
    }
    if (mark == CEDILLA) {
        composed[14] = 0x0C;
        composed[15] = 0x06;
    } else if (mark == BREVE) {
        composed[0] = 0x22;
        composed[1] = 0x1C;
    } else if (mark == UMLAUT) {
        composed[0] = 0x24;
        composed[1] = 0x24;
    } else if (mark == DOT) {
        composed[0] = 0x0C;
        composed[1] = 0x0C;
    } else if (mark == DOTLESS) {
        composed[0] = 0;
        composed[1] = 0;
    }
    return composed;
}

void display_draw_text_px(int x0, int y0, const char *text, display_color_t fg, display_color_t bg)
{
    const char *cursor = text;
    for (int i = 0; *cursor != '\0'; i++) {
        uint8_t composed[16];
        const uint8_t *glyph = character_glyph(next_character(&cursor), composed);
        int cx0 = x0 + i * 8;

        if (cx0 >= PANEL_WIDTH) {
            break;
        }
        if (cx0 + 8 <= 0) {
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
    // The pixel renderer clips by screen width in *glyphs*, not UTF-8 bytes.
    display_draw_text_px(col * 8, row * 16, text, color, s_active_bg);
}

void display_draw_text_centered(int row, const char *text, display_color_t color)
{
    int columns = 0;
    const char *cursor = text;
    while (*cursor != '\0') {
        next_character(&cursor);
        columns++;
    }
    int col = columns >= DISPLAY_COLS ? 0 : (DISPLAY_COLS - columns) / 2;
    display_draw_text_color(row, col, text, color);
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
