#include "rdm6300.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

// RDM6300 only transmits. Its 5V TX crosses the level shifter before this
// 3.3V input; no ESP32 TX pin is wired.
#define UART_PORT UART_NUM_1
#define UART_RX_GPIO 16 // Pico GP0
#define UART_TX_GPIO UART_PIN_NO_CHANGE

#define FRAME_LEN 14
#define STX 0x02
#define ETX 0x03

static const char *TAG = "rdm6300";

void rdm6300_init(void)
{
    uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_GPIO, UART_RX_GPIO,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "RDM6300 UART initialized on GPIO%d", UART_RX_GPIO);
}

static int hex_nibble(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool hex_byte(const uint8_t *ascii, uint8_t *out)
{
    int hi = hex_nibble(ascii[0]);
    int lo = hex_nibble(ascii[1]);
    if (hi < 0 || lo < 0) {
        return false;
    }
    *out = (uint8_t)((hi << 4) | lo);
    return true;
}

// Parses a 14-byte STX..ETX frame: STX + 10 hex chars (5 ID bytes) +
// 2 hex chars (checksum, XOR of the 5 ID bytes) + ETX.
static bool parse_frame(const uint8_t *frame, rdm6300_id_t *out_id)
{
    if (frame[0] != STX || frame[13] != ETX) {
        return false;
    }

    uint8_t id[5];
    for (int i = 0; i < 5; i++) {
        if (!hex_byte(&frame[1 + i * 2], &id[i])) {
            return false;
        }
    }

    uint8_t checksum;
    if (!hex_byte(&frame[11], &checksum)) {
        return false;
    }

    uint8_t computed = id[0] ^ id[1] ^ id[2] ^ id[3] ^ id[4];
    if (computed != checksum) {
        ESP_LOGW(TAG, "checksum mismatch, discarding frame");
        return false;
    }

    memcpy(out_id->bytes, id, 5);
    return true;
}

// Partial-frame state, carried across calls so a frame split across two
// poll ticks (e.g. UART FIFO not fully drained yet) doesn't get dropped
// and this function never has to wait/block for the rest to arrive.
static uint8_t s_frame_buf[FRAME_LEN];
static int s_frame_len = 0;

bool rdm6300_poll(rdm6300_id_t *out_id)
{
    // Drain whatever's in the UART FIFO right now -- zero timeout, so this
    // never blocks even mid-frame. Frames that straddle two poll calls just
    // resume from s_frame_len on the next call.
    uint8_t byte;
    while (uart_read_bytes(UART_PORT, &byte, 1, 0) == 1) {
        if (s_frame_len == 0 && byte != STX) {
            continue; // resync: skip stray bytes until STX
        }

        s_frame_buf[s_frame_len++] = byte;

        if (s_frame_len < FRAME_LEN) {
            continue; // frame not complete yet, keep accumulating
        }

        // Got a full frame's worth of bytes.
        bool ok = parse_frame(s_frame_buf, out_id);
        s_frame_len = 0; // reset regardless of parse result, to resync
        if (ok) {
            return true;
        }
        // fall through: keep draining in case more data is queued
    }

    return false;
}
