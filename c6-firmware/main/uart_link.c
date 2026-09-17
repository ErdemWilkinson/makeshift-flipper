#include "uart_link.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

#include "driver/uart.h"
#include "esp_log.h"

// Matches the wiring report from the P4 side: P4 GPIO18(TX)->C6 RX,
// P4 GPIO19(RX)->C6 TX. Actual C6 GPIO numbers depend on which pins you
// wired to the P4's 18/19 -- set these to match your board.
#define UART_PORT UART_NUM_1
#define UART_TX_GPIO 6
#define UART_RX_GPIO 7
#define UART_BAUD 115200

static const char *TAG = "uart_link";

void uart_link_init(void)
{
    uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_GPIO, UART_RX_GPIO,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART link to P4 initialized (TX=GPIO%d, RX=GPIO%d)", UART_TX_GPIO, UART_RX_GPIO);
}

void uart_link_read_line(char *out_line)
{
    int len = 0;
    for (;;) {
        uint8_t byte;
        int got = uart_read_bytes(UART_PORT, &byte, 1, portMAX_DELAY);
        if (got != 1) {
            continue;
        }
        if (byte == '\n') {
            out_line[len] = '\0';
            return;
        }
        if (byte != '\r' && len < UART_LINK_MAX_LINE_LEN - 1) {
            out_line[len++] = (char)byte;
        }
    }
}

void uart_link_write_line(const char *line)
{
    uart_write_bytes(UART_PORT, line, strlen(line));
    uart_write_bytes(UART_PORT, "\n", 1);
}
