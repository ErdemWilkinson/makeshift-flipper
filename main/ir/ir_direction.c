#include "ir_direction.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "driver/rmt_rx.h"
#include "esp_log.h"

// 4x VS1838B receivers, one per compass point, wired independently of the
// single-receiver ir_driver.c (which stays on GPIO1 for plain RX/TX use).
// None of these pins carry anything else in the current plan (OLED=7/8,
// RC522 SPI=9-13, RDM6300 RX=17, ESP32-C6=18/19, vibration=20,
// joystick=3/4/22/23, single IR RX/TX=1/2) -- rewire here if your board
// differs.
// NORTH/EAST sit on GPIO5/6, inside the ADC1-only GPIO0-6 range that
// buttons.c's own comment calls scarce (the joystick's X/Y axes already
// had to land on GPIO3/4 for the same reason). Using them here as plain
// digital RMT-RX inputs is fine today, but permanently forecloses using
// ADC on these two pins later without moving one of the two systems --
// worth remembering if a third analog input is ever needed.
#define GPIO_NORTH 5
#define GPIO_EAST  6
#define GPIO_SOUTH 14
#define GPIO_WEST  15

#define RMT_RESOLUTION_HZ 1000000 // 1 tick = 1us, matches ir_nec.h's *_us fields
#define MAX_RAW_SYMBOLS 64        // same bound as ir_driver.c: NEC frame is 33 symbols

static const char *TAG = "ir_direction";

typedef struct {
    rmt_channel_handle_t channel;
    QueueHandle_t queue;
    rmt_symbol_word_t raw_buf[MAX_RAW_SYMBOLS];
    ir_direction_flag_t flag;
    gpio_num_t gpio;
} receiver_t;

static receiver_t s_receivers[4];
static bool s_initialized;

static bool IRAM_ATTR rx_done_callback(rmt_channel_handle_t channel,
                                        const rmt_rx_done_event_data_t *edata,
                                        void *user_data)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)user_data, edata, &woken);
    return woken == pdTRUE;
}

static void start_next_rx(receiver_t *rx)
{
    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = 9000000,
    };
    ESP_ERROR_CHECK(rmt_receive(rx->channel, rx->raw_buf, sizeof(rx->raw_buf), &rx_cfg));
}

static void receiver_init(receiver_t *rx, gpio_num_t gpio, ir_direction_flag_t flag)
{
    rx->gpio = gpio;
    rx->flag = flag;

    rmt_rx_channel_config_t rx_chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 128,
        .gpio_num = gpio,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_cfg, &rx->channel));

    rx->queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t rx_cbs = {
        .on_recv_done = rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx->channel, &rx_cbs, rx->queue));
    ESP_ERROR_CHECK(rmt_enable(rx->channel));
    start_next_rx(rx);
}

void ir_direction_init(void)
{
    receiver_init(&s_receivers[0], GPIO_NORTH, IR_DIR_NORTH);
    receiver_init(&s_receivers[1], GPIO_EAST,  IR_DIR_EAST);
    receiver_init(&s_receivers[2], GPIO_SOUTH, IR_DIR_SOUTH);
    receiver_init(&s_receivers[3], GPIO_WEST,  IR_DIR_WEST);

    s_initialized = true;

    ESP_LOGI(TAG, "IR direction finder initialized (N=GPIO%d E=GPIO%d S=GPIO%d W=GPIO%d)",
             GPIO_NORTH, GPIO_EAST, GPIO_SOUTH, GPIO_WEST);
}

bool ir_direction_is_available(void)
{
    return s_initialized;
}

bool ir_direction_poll(uint8_t *out_flags, ir_nec_frame_t *out_frame)
{
    if (!s_initialized) {
        *out_flags = 0;
        return false;
    }

    uint8_t flags = 0;
    bool got_frame = false;

    // Drain all 4 receivers every call (not just until the first hit) so a
    // source near the boundary between two receivers, which both catch the
    // same transmission, is reported as both flags rather than just one.
    for (int i = 0; i < 4; i++) {
        receiver_t *rx = &s_receivers[i];
        rmt_rx_done_event_data_t event;
        if (xQueueReceive(rx->queue, &event, 0) != pdTRUE) {
            continue;
        }

        ir_raw_symbol_t symbols[MAX_RAW_SYMBOLS];
        size_t count = event.num_symbols;
        if (count > MAX_RAW_SYMBOLS) {
            count = MAX_RAW_SYMBOLS;
        }
        for (size_t s = 0; s < count; s++) {
            symbols[s].mark_us = event.received_symbols[s].duration0;
            symbols[s].space_us = event.received_symbols[s].duration1;
        }

        ir_nec_frame_t frame;
        if (ir_nec_decode(symbols, count, &frame)) {
            flags |= rx->flag;
            if (!got_frame) {
                *out_frame = frame;
                got_frame = true;
            }
        }

        start_next_rx(rx);
    }

    *out_flags = flags;
    return got_frame;
}
