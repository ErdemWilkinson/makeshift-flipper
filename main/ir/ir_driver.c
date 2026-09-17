#include "ir_driver.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"

// Empty pins in the current plan (OLED=7/8, RC522 SPI=9-13, joystick=14-22,
// RDM6300 RX=17, ESP32-C6=18/19, vibration motor=20). Rewire here if these
// end up conflicting with anything physically wired differently.
#define IR_RX_GPIO 1 // VS1838B OUT
#define IR_TX_GPIO 2 // IR LED (through a driver transistor)

#define RMT_RESOLUTION_HZ 1000000 // 1 tick = 1us, matches ir_nec.h's *_us fields

static const char *TAG = "ir_driver";

static rmt_channel_handle_t s_rx_channel;
static rmt_channel_handle_t s_tx_channel;
static rmt_encoder_handle_t s_tx_copy_encoder;
static QueueHandle_t s_rx_queue;

// Generous upper bound: an NEC frame is 33 symbols; repeat codes are single
// symbols. 64 covers any frame we care about with room to spare.
#define MAX_RAW_SYMBOLS 64
static rmt_symbol_word_t s_rx_raw_buf[MAX_RAW_SYMBOLS];

static bool IRAM_ATTR rx_done_callback(rmt_channel_handle_t channel,
                                        const rmt_rx_done_event_data_t *edata,
                                        void *user_data)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)user_data, edata, &woken);
    return woken == pdTRUE;
}

static void start_next_rx(void)
{
    rmt_receive_config_t rx_cfg = {
        // NEC's shortest meaningful space is ~562us (bit-0 space); its
        // longest is the ~4.5ms leader space. Bound the window around that
        // so noise doesn't get treated as a frame boundary.
        .signal_range_min_ns = 1000,       // 1us: ignore sub-microsecond glitches
        .signal_range_max_ns = 9000000,    // 9ms: NEC's longest expected space
    };
    ESP_ERROR_CHECK(rmt_receive(s_rx_channel, s_rx_raw_buf, sizeof(s_rx_raw_buf), &rx_cfg));
}

void ir_driver_init(void)
{
    rmt_rx_channel_config_t rx_chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 128,
        .gpio_num = IR_RX_GPIO,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_cfg, &s_rx_channel));

    // Depth 3, not 1: the main loop polls ir_driver_poll_rx() every ~10ms
    // (main.c), and back-to-back IR bursts (e.g. a remote's repeat codes,
    // or two different transmitters) arriving faster than that would
    // previously overflow a depth-1 queue and get silently dropped by
    // xQueueSendFromISR in rx_done_callback().
    s_rx_queue = xQueueCreate(3, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t rx_cbs = {
        .on_recv_done = rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx_channel, &rx_cbs, s_rx_queue));
    ESP_ERROR_CHECK(rmt_enable(s_rx_channel));
    start_next_rx();

    rmt_tx_channel_config_t tx_chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = IR_TX_GPIO,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_cfg, &s_tx_channel));

    // NEC is carrier-modulated at 38kHz, ~1/3 duty cycle.
    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = 38000,
        .duty_cycle = 0.33,
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(s_tx_channel, &carrier_cfg));
    ESP_ERROR_CHECK(rmt_enable(s_tx_channel));

    rmt_copy_encoder_config_t copy_encoder_cfg = {0};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_cfg, &s_tx_copy_encoder));

    ESP_LOGI(TAG, "IR driver initialized (RX=GPIO%d, TX=GPIO%d)", IR_RX_GPIO, IR_TX_GPIO);
}

bool ir_driver_poll_rx(ir_nec_frame_t *out_frame)
{
    rmt_rx_done_event_data_t event;
    if (xQueueReceive(s_rx_queue, &event, 0) != pdTRUE) {
        return false;
    }

    // rmt_symbol_word_t packs mark+space into duration0/level0 and
    // duration1/level1; our ir_raw_symbol_t only cares about the two
    // durations (level is implied: mark=IR on, space=IR off).
    ir_raw_symbol_t symbols[MAX_RAW_SYMBOLS];
    size_t count = event.num_symbols;
    if (count > MAX_RAW_SYMBOLS) {
        count = MAX_RAW_SYMBOLS;
    }
    for (size_t i = 0; i < count; i++) {
        symbols[i].mark_us = event.received_symbols[i].duration0;
        symbols[i].space_us = event.received_symbols[i].duration1;
    }

    bool ok = ir_nec_decode(symbols, count, out_frame);

    start_next_rx();
    return ok;
}

void ir_driver_send(const ir_nec_frame_t *frame)
{
    ir_raw_symbol_t raw[IR_NEC_MAX_SYMBOLS];
    size_t count = ir_nec_encode(frame, raw);

    rmt_symbol_word_t tx_buf[IR_NEC_MAX_SYMBOLS];
    for (size_t i = 0; i < count; i++) {
        tx_buf[i].level0 = 1;
        tx_buf[i].duration0 = raw[i].mark_us;
        tx_buf[i].level1 = 0;
        tx_buf[i].duration1 = raw[i].space_us;
    }

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    ESP_ERROR_CHECK(rmt_transmit(s_tx_channel, s_tx_copy_encoder, tx_buf,
                                  count * sizeof(rmt_symbol_word_t), &tx_cfg));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_tx_channel, pdMS_TO_TICKS(100)));
}
