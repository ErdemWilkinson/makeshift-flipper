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
#define REG_COMMAND        0x01
#define REG_COM_IRQ        0x04
#define REG_DIV_IRQ        0x05
#define REG_ERROR          0x06
#define REG_STATUS2        0x08
#define REG_FIFO_DATA      0x09
#define REG_FIFO_LEVEL     0x0A
#define REG_CONTROL        0x0C
#define REG_BIT_FRAMING    0x0D
#define REG_COLL           0x0E
#define REG_MODE           0x11
#define REG_TX_CONTROL     0x14
#define REG_TX_ASK         0x15
#define REG_CRC_RESULT_MSB 0x21
#define REG_CRC_RESULT_LSB 0x22
#define REG_T_MODE         0x2A
#define REG_T_PRESCALER    0x2B
#define REG_T_RELOAD_HI    0x2C
#define REG_T_RELOAD_LO    0x2D

// --- MFRC522 commands ---
#define CMD_IDLE          0x00
#define CMD_CALCCRC       0x03
#define CMD_TRANSCEIVE    0x0C
#define CMD_MFAUTHENT     0x0E
#define CMD_SOFT_RESET    0x0F

// PICC commands
#define PICC_REQA         0x26
#define PICC_ANTICOLL_CL1 0x93
#define PICC_SELECT_CL1   0x93
#define PICC_AUTHENT_1A   0x60
#define PICC_AUTHENT_1B   0x61
#define PICC_READ         0x30
#define PICC_WRITE        0xA0
#define PICC_HALT         0x50

// Mifare Classic default/well-known keys, tried in order against every
// sector when doing a full-card dump. Not exhaustive -- see rc522.h.
const rc522_key_t RC522_DEFAULT_KEYS[] = {
    {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}}, // factory default
    {{0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5}},
    {{0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}},
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {{0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5}},
    {{0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD}},
    {{0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A}},
    {{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}},
};
const int RC522_DEFAULT_KEY_COUNT = sizeof(RC522_DEFAULT_KEYS) / sizeof(RC522_DEFAULT_KEYS[0]);

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

// Runs the MFRC522's hardware CRC_A co-processor (CalcCRC command) over
// `data`, writing the little-endian 2-byte result to `out_crc`. Used to
// append/verify CRC_A on the commands that need it (SELECT, READ, WRITE) --
// REQA and the anticollision command above are short frames that don't
// carry a CRC_A per ISO14443-3, so transceive() alone is fine for those.
static bool calc_crc(const uint8_t *data, uint8_t len, uint8_t out_crc[2])
{
    spi_write_reg(REG_COMMAND, CMD_IDLE);
    spi_write_reg(REG_DIV_IRQ, 0x04);    // clear CRCIRq
    spi_write_reg(REG_FIFO_LEVEL, 0x80); // FlushBuffer

    for (int i = 0; i < len; i++) {
        spi_write_reg(REG_FIFO_DATA, data[i]);
    }
    spi_write_reg(REG_COMMAND, CMD_CALCCRC);

    bool completed = false;
    for (int i = 0; i < 100; i++) {
        if (spi_read_reg(REG_DIV_IRQ) & 0x04) { // CRCIRq
            completed = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    spi_write_reg(REG_COMMAND, CMD_IDLE);

    if (!completed || s_spi_fault) {
        return false;
    }

    out_crc[0] = spi_read_reg(REG_CRC_RESULT_LSB);
    out_crc[1] = spi_read_reg(REG_CRC_RESULT_MSB);
    return true;
}

// Same as transceive(), but appends a CRC_A to `tx` before sending and
// strips + verifies a trailing CRC_A on the reply. Used for every command
// past anticollision (SELECT, MFAuthent's own framing excluded -- that one
// doesn't carry a software CRC_A, see rc522_authenticate()), READ, WRITE.
// Returns the data length (CRC stripped) on success, -1 on any failure
// (SPI fault, timeout, or CRC mismatch on the reply).
static int transceive_with_crc(const uint8_t *tx, uint8_t tx_len,
                                uint8_t *rx, uint8_t rx_buf_len)
{
    uint8_t crc[2];
    if (!calc_crc(tx, tx_len, crc)) {
        return -1;
    }

    uint8_t full_tx[18]; // largest caller (WRITE data phase) is 16 + 2
    if (tx_len + 2 > sizeof(full_tx)) {
        return -1;
    }
    memcpy(full_tx, tx, tx_len);
    full_tx[tx_len] = crc[0];
    full_tx[tx_len + 1] = crc[1];

    uint8_t raw_rx[20];
    int raw_len = transceive(full_tx, tx_len + 2, raw_rx, sizeof(raw_rx), 0x00);
    if (raw_len < 0) {
        return -1;
    }

    // A 4-bit ACK (WRITE's two phases) has no CRC_A of its own -- pass it
    // through as-is rather than trying to strip/verify a CRC that isn't
    // there.
    if (raw_len <= 1) {
        if (raw_len > rx_buf_len) {
            return -1;
        }
        memcpy(rx, raw_rx, raw_len);
        return raw_len;
    }

    if (raw_len < 2) {
        return -1;
    }
    int data_len = raw_len - 2;
    uint8_t reply_crc[2];
    if (!calc_crc(raw_rx, data_len, reply_crc)) {
        return -1;
    }
    if (reply_crc[0] != raw_rx[data_len] || reply_crc[1] != raw_rx[data_len + 1]) {
        ESP_LOGW(TAG, "reply CRC_A mismatch, discarding");
        return -1;
    }

    if (data_len > rx_buf_len) {
        data_len = rx_buf_len;
    }
    memcpy(rx, raw_rx, data_len);
    return data_len;
}

// REQA + anticollision + SELECT, cascade level 1 only (4-byte UIDs -- same
// limitation as rc522_read_uid()). Needed before MFAuthent: the PICC must
// be in ACTIVE state, which anticollision alone doesn't reach. Returns
// false if the card isn't there or doesn't match `uid` anymore.
static bool select_card(const rc522_uid_t *uid)
{
    uint8_t atqa[2];
    uint8_t req_cmd = PICC_REQA;
    reg_clear_bits(REG_COLL, 0x80);
    if (transceive(&req_cmd, 1, atqa, sizeof(atqa), 0x07) < 1) {
        return false;
    }

    reg_clear_bits(REG_COLL, 0x80);
    uint8_t anticoll_cmd[2] = { PICC_ANTICOLL_CL1, 0x20 };
    uint8_t uid_resp[5];
    if (transceive(anticoll_cmd, 2, uid_resp, sizeof(uid_resp), 0x00) != 5) {
        return false;
    }
    if (memcmp(uid_resp, uid->bytes, 4) != 0) {
        return false; // a different card answered -- not the one we scanned
    }

    // SELECT: NVB=0x70 (all 40 bits known: UID+BCC), CRC_A required.
    uint8_t select_cmd[7] = { PICC_SELECT_CL1, 0x70,
                               uid_resp[0], uid_resp[1], uid_resp[2], uid_resp[3], uid_resp[4] };
    uint8_t sak[3];
    int sak_len = transceive_with_crc(select_cmd, 7, sak, sizeof(sak));
    return sak_len == 1; // 1-byte SAK (CRC already stripped/verified)
}

bool rc522_authenticate(const rc522_uid_t *uid, uint8_t block_addr,
                         rc522_key_type_t key_type, const rc522_key_t *key)
{
    s_spi_fault = false;

    if (!select_card(uid)) {
        return false;
    }

    // MFAuthent's own frame isn't CRC_A-protected (the crypto exchange
    // itself is the integrity check) -- plain FIFO write + command, no
    // transceive_with_crc() here.
    uint8_t cmd[12];
    cmd[0] = (key_type == RC522_KEY_A) ? PICC_AUTHENT_1A : PICC_AUTHENT_1B;
    cmd[1] = block_addr;
    memcpy(&cmd[2], key->bytes, 6);
    memcpy(&cmd[8], uid->bytes, 4);

    spi_write_reg(REG_COMMAND, CMD_IDLE);
    spi_write_reg(REG_COM_IRQ, 0x7F);
    spi_write_reg(REG_FIFO_LEVEL, 0x80);
    for (int i = 0; i < (int)sizeof(cmd); i++) {
        spi_write_reg(REG_FIFO_DATA, cmd[i]);
    }
    spi_write_reg(REG_COMMAND, CMD_MFAUTHENT);

    bool completed = false;
    for (int i = 0; i < 200; i++) {
        uint8_t irq = spi_read_reg(REG_COM_IRQ);
        if (irq & 0x10) { // IdleIRq
            completed = true;
            break;
        }
        if (irq & 0x01) { // TimerIRq
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (!completed || s_spi_fault) {
        return false;
    }

    // MFCrypto1On (Status2Reg bit 3) is the hardware's own confirmation
    // that the key was accepted and the session is now encrypted.
    return (spi_read_reg(REG_STATUS2) & 0x08) != 0;
}

bool rc522_read_block(uint8_t block_addr, uint8_t out_data[RC522_BLOCK_SIZE])
{
    s_spi_fault = false;
    uint8_t cmd[2] = { PICC_READ, block_addr };
    uint8_t rx[RC522_BLOCK_SIZE];
    int len = transceive_with_crc(cmd, 2, rx, sizeof(rx));
    if (len != RC522_BLOCK_SIZE || s_spi_fault) {
        return false;
    }
    memcpy(out_data, rx, RC522_BLOCK_SIZE);
    return true;
}

bool rc522_write_block(uint8_t block_addr, const uint8_t data[RC522_BLOCK_SIZE])
{
    s_spi_fault = false;

    uint8_t cmd[2] = { PICC_WRITE, block_addr };
    uint8_t ack[1];
    if (transceive_with_crc(cmd, 2, ack, sizeof(ack)) != 1 || (ack[0] & 0x0F) != 0x0A) {
        return false;
    }

    uint8_t ack2[1];
    if (transceive_with_crc(data, RC522_BLOCK_SIZE, ack2, sizeof(ack2)) != 1
        || (ack2[0] & 0x0F) != 0x0A) {
        return false;
    }

    return !s_spi_fault;
}

void rc522_stop_crypto(void)
{
    uint8_t halt_cmd[2] = { PICC_HALT, 0x00 };
    uint8_t dummy[2];
    transceive_with_crc(halt_cmd, 2, dummy, sizeof(dummy)); // reply, if any, is ignored
    reg_clear_bits(REG_STATUS2, 0x08); // clear MFCrypto1On
}

// Gen1a magic-card backdoor: command 0x40 (7-bit short frame) followed by
// 0x43 (8-bit), each needing a valid reply before the card accepts further
// writes with no authentication. A genuine card (or a gen2 clone that only
// accepts normal MFAuthent) simply won't answer 0x40 -- that's the "not
// magic" signal, distinct from an SPI/bus fault.
bool rc522_gen1a_write_block0(const uint8_t block0_data[RC522_BLOCK_SIZE],
                               bool *out_is_magic)
{
    s_spi_fault = false;
    *out_is_magic = false;

    reg_clear_bits(REG_COLL, 0x80);
    uint8_t unlock1 = 0x40;
    uint8_t resp1[2];
    if (transceive(&unlock1, 1, resp1, sizeof(resp1), 0x07) < 1 || s_spi_fault) {
        return false; // no reply at all -- not a gen1a card
    }

    uint8_t unlock2 = 0x43;
    uint8_t resp2[2];
    if (transceive(&unlock2, 1, resp2, sizeof(resp2), 0x00) < 1 || s_spi_fault) {
        return false;
    }

    *out_is_magic = true; // backdoor accepted -- treat write failures below
                           // as real failures, not "wrong card type"

    uint8_t cmd[2] = { PICC_WRITE, 0x00 };
    uint8_t ack[1];
    if (transceive_with_crc(cmd, 2, ack, sizeof(ack)) != 1 || (ack[0] & 0x0F) != 0x0A) {
        return false;
    }

    uint8_t ack2[1];
    if (transceive_with_crc(block0_data, RC522_BLOCK_SIZE, ack2, sizeof(ack2)) != 1
        || (ack2[0] & 0x0F) != 0x0A) {
        return false;
    }

    return !s_spi_fault;
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
