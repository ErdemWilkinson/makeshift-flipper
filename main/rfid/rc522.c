#include "rc522.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

// Matches the wiring report: SCK=10, MOSI=11, MISO=12, CS(SDA)=9, RST=13.
#define PIN_SCK  10
#define PIN_MOSI 11
#define PIN_MISO 12
#define PIN_CS   9
#define PIN_RST  13

#define SPI_HOST_USED SPI2_HOST
#define SPI_CLOCK_HZ  (4 * 1000 * 1000) // MFRC522 supports up to 10MHz; 4MHz is a safe margin

static const char *TAG = "rc522";
static spi_device_handle_t s_spi;

// Set by spi_write_reg()/spi_read_reg() whenever the underlying SPI
// transaction itself fails (wiring/bus problem), as opposed to the RC522
// responding normally with "no card" or a protocol error. Checked by
// rc522_read_uid() so a real hardware fault surfaces as RC522_SCAN_ERROR
// instead of silently looking like "no card" (spi_read_reg returning 0 on
// failure is indistinguishable from a real register value of 0 otherwise).
static bool s_spi_fault = false;

// --- MFRC522 register addresses (subset actually used here) ---
#define REG_COMMAND       0x01
#define REG_COM_IRQ       0x04
#define REG_ERROR         0x06
#define REG_FIFO_DATA     0x09
#define REG_FIFO_LEVEL    0x0A
#define REG_CONTROL       0x0C
#define REG_BIT_FRAMING   0x0D
#define REG_COLL          0x0E
#define REG_MODE          0x11
#define REG_TX_CONTROL    0x14
#define REG_TX_ASK        0x15
#define REG_T_MODE        0x2A
#define REG_T_PRESCALER   0x2B
#define REG_T_RELOAD_HI   0x2C
#define REG_T_RELOAD_LO   0x2D

// --- MFRC522 commands ---
#define CMD_IDLE          0x00
#define CMD_TRANSCEIVE    0x0C
#define CMD_SOFT_RESET    0x0F

// PICC commands
#define PICC_REQA         0x26
#define PICC_ANTICOLL_CL1 0x93

static void spi_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1) & 0x7E), value };
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
    };
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI write failed: %s", esp_err_to_name(err));
        s_spi_fault = true;
    }
}

static uint8_t spi_read_reg(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(((reg << 1) & 0x7E) | 0x80), 0x00 };
    uint8_t rx[2] = {0};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI read failed: %s", esp_err_to_name(err));
        s_spi_fault = true;
        return 0;
    }
    return rx[1];
}

static void reg_set_bits(uint8_t reg, uint8_t mask)
{
    spi_write_reg(reg, spi_read_reg(reg) | mask);
}

static void reg_clear_bits(uint8_t reg, uint8_t mask)
{
    spi_write_reg(reg, spi_read_reg(reg) & ~mask);
}

void rc522_antenna_on(void)
{
    uint8_t value = spi_read_reg(REG_TX_CONTROL);
    if ((value & 0x03) != 0x03) {
        reg_set_bits(REG_TX_CONTROL, 0x03);
    }
}

void rc522_antenna_off(void)
{
    reg_clear_bits(REG_TX_CONTROL, 0x03);
}

void rc522_init(void)
{
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(PIN_RST, 1);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_USED, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST_USED, &dev_cfg, &s_spi));

    // Soft reset, then the standard MFRC522 init sequence (timer + TX ASK
    // modulation + default mode register), per the datasheet's "Init" flow.
    spi_write_reg(REG_COMMAND, CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(50));

    spi_write_reg(REG_T_MODE, 0x8D);
    spi_write_reg(REG_T_PRESCALER, 0x3E);
    spi_write_reg(REG_T_RELOAD_LO, 30);
    spi_write_reg(REG_T_RELOAD_HI, 0);
    spi_write_reg(REG_TX_ASK, 0x40);
    spi_write_reg(REG_MODE, 0x3D);

    // Antenna starts OFF to save power; callers turn it on only while a
    // scan screen is actually active (see rc522_antenna_on()).
    rc522_antenna_off();

    ESP_LOGI(TAG, "RC522 initialized");
}

// Sends `tx` via CMD_TRANSCEIVE and collects the response into `rx`.
// Returns the number of bits in the last received byte via `out_valid_bits`
// (0 means "all 8 bits valid"), or -1 on timeout/error.
static int transceive(const uint8_t *tx, uint8_t tx_len,
                       uint8_t *rx, uint8_t rx_buf_len, uint8_t tx_last_bits)
{
    spi_write_reg(REG_COMMAND, CMD_IDLE);
    spi_write_reg(REG_COM_IRQ, 0x7F);     // clear all IRQ flags
    spi_write_reg(REG_FIFO_LEVEL, 0x80);  // FlushBuffer bit

    for (int i = 0; i < tx_len; i++) {
        spi_write_reg(REG_FIFO_DATA, tx[i]);
    }

    spi_write_reg(REG_BIT_FRAMING, tx_last_bits);
    spi_write_reg(REG_COMMAND, CMD_TRANSCEIVE);
    reg_set_bits(REG_BIT_FRAMING, 0x80); // StartSend

    // Poll for RxIRq or Timer IRQ, with a bounded number of tries instead
    // of a hardware timeout interrupt (keeps this a plain polling driver).
    bool completed = false;
    for (int i = 0; i < 200; i++) {
        uint8_t irq = spi_read_reg(REG_COM_IRQ);
        if (irq & 0x30) { // RxIRq or IdleIRq
            completed = true;
            break;
        }
        if (irq & 0x01) { // TimerIRq: no card responded
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    reg_clear_bits(REG_BIT_FRAMING, 0x80); // stop StartSend

    if (!completed) {
        return -1;
    }

    uint8_t err = spi_read_reg(REG_ERROR);
    if (err & 0x13) { // BufferOvfl | ParityErr | ProtocolErr
        return -1;
    }

    uint8_t fifo_len = spi_read_reg(REG_FIFO_LEVEL);
    if (fifo_len > rx_buf_len) {
        fifo_len = rx_buf_len;
    }
    for (int i = 0; i < fifo_len; i++) {
        rx[i] = spi_read_reg(REG_FIFO_DATA);
    }

    return fifo_len;
}

// ISO14443-3 cascade tag: the first UID byte is 0x88 when the real UID is
// 7 or 10 bytes long (cascade level 2/3 needed to get the rest). This
// driver only implements cascade level 1 (4-byte UIDs), so seeing this tag
// means "real card, but this driver can't read its full UID yet" rather
// than "no card" or "corrupted read" -- worth telling the UI apart from
// both of those.
#define CASCADE_TAG 0x88

rc522_scan_result_t rc522_read_uid(rc522_uid_t *out_uid)
{
    s_spi_fault = false;

    // REQA: 7-bit short frame, no CRC. A successful ATQA reply means a
    // PICC is in the field.
    uint8_t atqa[2];
    uint8_t req_cmd = PICC_REQA;
    reg_clear_bits(REG_COLL, 0x80);
    int atqa_len = transceive(&req_cmd, 1, atqa, sizeof(atqa), 0x07);
    if (atqa_len < 1) {
        // A SPI bus fault makes every register read return 0, which looks
        // exactly like "TimerIRq never fired" (no card) -- tell them apart
        // so a wiring/bus problem doesn't masquerade as "just no card yet".
        return s_spi_fault ? RC522_SCAN_ERROR : RC522_SCAN_NO_CARD;
    }

    // Anticollision, cascade level 1: ask for the full UID (no known bits
    // yet, so NVB = 0x20 and we send just the command byte).
    reg_clear_bits(REG_COLL, 0x80);
    uint8_t anticoll_cmd[2] = { PICC_ANTICOLL_CL1, 0x20 };
    uint8_t uid_resp[5]; // 4 UID bytes + 1 BCC
    int uid_len = transceive(anticoll_cmd, 2, uid_resp, sizeof(uid_resp), 0x00);
    if (uid_len != 5) {
        return RC522_SCAN_ERROR;
    }

    uint8_t bcc = uid_resp[0] ^ uid_resp[1] ^ uid_resp[2] ^ uid_resp[3];
    if (bcc != uid_resp[4]) {
        ESP_LOGW(TAG, "UID BCC mismatch, discarding read");
        return RC522_SCAN_ERROR;
    }

    if (uid_resp[0] == CASCADE_TAG) {
        // 7/10-byte UID -- correctly read (BCC checks out) but this driver
        // only implements cascade level 1, so the full UID isn't available.
        return RC522_SCAN_UNSUPPORTED_UID;
    }

    if (s_spi_fault) {
        // Bytes came back (possibly stale/garbage from a flaky bus) but at
        // least one transaction along the way failed -- don't trust a UID
        // assembled from a mix of good and failed reads.
        return RC522_SCAN_ERROR;
    }

    memcpy(out_uid->bytes, uid_resp, 4);
    out_uid->length = 4;
    return RC522_SCAN_OK;
}
