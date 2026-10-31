#include "rfid.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---------------------------------------------------------
 * MFRC522 REGISTER DEFINITIONS
 * --------------------------------------------------------- */

#define CommandReg       0x01
#define CommIEnReg       0x02
#define DivIEnReg        0x03
#define CommIrqReg       0x04
#define DivIrqReg        0x05
#define ErrorReg         0x06
#define Status2Reg       0x08
#define FIFODataReg      0x09
#define FIFOLevelReg     0x0A
#define ControlReg       0x0C
#define BitFramingReg    0x0D
#define CollReg          0x0E

#define ModeReg          0x11
#define TxControlReg     0x14
#define TxASKReg         0x15
#define TModeReg         0x2A
#define TPrescalerReg    0x2B
#define TReloadRegH      0x2C
#define TReloadRegL      0x2D
#define VersionReg       0x37

#define CRCResultRegH    0x21
#define CRCResultRegL    0x22

/* ---------------------------------------------------------
 * MFRC522 COMMANDS
 * --------------------------------------------------------- */

#define PCD_IDLE         0x00
#define PCD_CALCCRC      0x03
#define PCD_TRANSCEIVE   0x0C
#define PCD_SOFTRESET    0x0F

#define PICC_REQA        0x26
#define PICC_WUPA        0x52
#define PICC_SEL_CL1     0x93
#define PICC_SEL_CL2     0x95
#define PICC_SEL_CL3     0x97
#define PICC_HLTA        0x50

#define PICC_CT          0x88

#define RFID_SPI_HOST    SPI2_HOST
#define RFID_SPI_SPEED   1000000

#define RFID_MAX_FRAME   64
#define RFID_MAX_TRANSFER (RFID_MAX_FRAME + 1)
#define RFID_TIMEOUT_US  100000

static const char *TAG = "RFID";

/* ---------------------------------------------------------
 * SPI BUS INITIALIZATION
 * --------------------------------------------------------- */

esp_err_t rfid_spi_bus_init(void)
{
    static bool bus_initialized = false;

    if (bus_initialized) {
        return ESP_OK;
    }

    spi_bus_config_t bus_config = {
        .mosi_io_num = GPIO_NUM_23,
        .miso_io_num = GPIO_NUM_19,
        .sclk_io_num = GPIO_NUM_18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = RFID_MAX_TRANSFER
    };

    esp_err_t result = spi_bus_initialize(
        RFID_SPI_HOST,
        &bus_config,
        SPI_DMA_DISABLED
    );

    if (result == ESP_OK) {
        bus_initialized = true;
        ESP_LOGI(TAG, "Shared RFID SPI bus initialized");
    } else if (result == ESP_ERR_INVALID_STATE) {
        /* The bus may already have been initialized elsewhere. */
        bus_initialized = true;
        ESP_LOGW(TAG, "SPI bus was already initialized");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "SPI bus initialization failed: %s",
                 esp_err_to_name(result));
    }

    return result;
}

/* ---------------------------------------------------------
 * LOW-LEVEL REGISTER ACCESS
 * --------------------------------------------------------- */

static esp_err_t write_register(
    rfid_reader_t *reader,
    uint8_t reg,
    uint8_t value
)
{
    uint8_t tx_data[2];

    tx_data[0] = (uint8_t)((reg << 1) & 0x7E);
    tx_data[1] = value;

    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx_data
    };

    return spi_device_polling_transmit(
        reader->spi,
        &transaction
    );
}

static esp_err_t read_register(
    rfid_reader_t *reader,
    uint8_t reg,
    uint8_t *value
)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_data[2] = {
        (uint8_t)(((reg << 1) & 0x7E) | 0x80),
        0x00
    };

    uint8_t rx_data[2] = {0};

    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    esp_err_t result = spi_device_polling_transmit(
        reader->spi,
        &transaction
    );

    if (result != ESP_OK) {
        return result;
    }

    *value = rx_data[1];
    return ESP_OK;
}

static esp_err_t set_register_bits(
    rfid_reader_t *reader,
    uint8_t reg,
    uint8_t mask
)
{
    uint8_t value;
    esp_err_t result = read_register(reader, reg, &value);

    if (result != ESP_OK) {
        return result;
    }

    return write_register(reader, reg, value | mask);
}

static esp_err_t clear_register_bits(
    rfid_reader_t *reader,
    uint8_t reg,
    uint8_t mask
)
{
    uint8_t value;
    esp_err_t result = read_register(reader, reg, &value);

    if (result != ESP_OK) {
        return result;
    }

    return write_register(reader, reg, value & (uint8_t)(~mask));
}

/* ---------------------------------------------------------
 * FIFO HELPERS
 * --------------------------------------------------------- */

static esp_err_t clear_fifo(rfid_reader_t *reader)
{
    esp_err_t result = write_register(reader, FIFOLevelReg, 0x80);

    if (result != ESP_OK) {
        return result;
    }

    return ESP_OK;
}

static esp_err_t write_fifo(
    rfid_reader_t *reader,
    const uint8_t *data,
    uint8_t length
)
{
    if (data == NULL || length == 0 || length > RFID_MAX_FRAME) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_data[RFID_MAX_TRANSFER];
    tx_data[0] = (uint8_t)((FIFODataReg << 1) & 0x7E);
    memcpy(&tx_data[1], data, length);

    spi_transaction_t transaction = {
        .length = (size_t)(length + 1) * 8,
        .tx_buffer = tx_data
    };

    return spi_device_polling_transmit(reader->spi, &transaction);
}

static esp_err_t read_fifo(
    rfid_reader_t *reader,
    uint8_t *data,
    uint8_t length
)
{
    if (data == NULL || length == 0 || length > RFID_MAX_FRAME) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_data[RFID_MAX_TRANSFER] = {0};
    uint8_t rx_data[RFID_MAX_TRANSFER] = {0};
    tx_data[0] = (uint8_t)(((FIFODataReg << 1) & 0x7E) | 0x80);

    spi_transaction_t transaction = {
        .length = (size_t)(length + 1) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    esp_err_t result = spi_device_polling_transmit(
        reader->spi,
        &transaction
    );
    if (result != ESP_OK) {
        return result;
    }

    memcpy(data, &rx_data[1], length);
    return ESP_OK;
}

/* ---------------------------------------------------------
 * MFRC522 CRC_A CALCULATION
 * --------------------------------------------------------- */

static esp_err_t calculate_crc(
    rfid_reader_t *reader,
    const uint8_t *data,
    uint8_t length,
    uint8_t *crc_low,
    uint8_t *crc_high
)
{
    if (data == NULL || crc_low == NULL || crc_high == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result;

    result = write_register(reader, CommandReg, PCD_IDLE);
    if (result != ESP_OK) return result;

    result = write_register(reader, DivIrqReg, 0x04);
    if (result != ESP_OK) return result;

    result = clear_fifo(reader);
    if (result != ESP_OK) return result;

    result = write_fifo(reader, data, length);
    if (result != ESP_OK) return result;

    result = write_register(reader, CommandReg, PCD_CALCCRC);
    if (result != ESP_OK) return result;

    bool completed = false;

    for (int i = 0; i < 2000; i++) {
        uint8_t irq;

        result = read_register(reader, DivIrqReg, &irq);
        if (result != ESP_OK) return result;

        if (irq & 0x04) {
            completed = true;
            break;
        }

        esp_rom_delay_us(50);
    }

    if (!completed) {
        return ESP_ERR_TIMEOUT;
    }

    result = read_register(reader, CRCResultRegL, crc_low);
    if (result != ESP_OK) return result;

    result = read_register(reader, CRCResultRegH, crc_high);
    if (result != ESP_OK) return result;

    return ESP_OK;
}

/* ---------------------------------------------------------
 * TRANSCEIVE
 *
 * Sends a command to the card and receives its response.
 * Only RxIRq indicates that card data was received. IdleIRq can also be
 * raised when the command ends without a card response.
 * --------------------------------------------------------- */

static esp_err_t transceive(
    rfid_reader_t *reader,
    const uint8_t *send_data,
    uint8_t send_length,
    uint8_t valid_bits,
    uint8_t *receive_data,
    uint8_t *receive_length,
    uint8_t *last_bits
)
{
    if (reader == NULL || send_data == NULL ||
        receive_data == NULL || receive_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result;

    *receive_length = 0;

    if (last_bits != NULL) {
        *last_bits = 0;
    }

    result = write_register(reader, CommandReg, PCD_IDLE);
    if (result != ESP_OK) return result;

    /* Clear interrupt flags. */
    result = write_register(reader, CommIrqReg, 0x7F);
    if (result != ESP_OK) return result;

    result = clear_fifo(reader);
    if (result != ESP_OK) return result;

    result = write_fifo(reader, send_data, send_length);
    if (result != ESP_OK) return result;

    result = write_register(reader, BitFramingReg, valid_bits);
    if (result != ESP_OK) return result;

    result = write_register(reader, CommandReg, PCD_TRANSCEIVE);
    if (result != ESP_OK) return result;

    result = set_register_bits(reader, BitFramingReg, 0x80);
    if (result != ESP_OK) return result;

    bool received = false;
    bool timer_expired = false;
    bool command_idle = false;

    for (int i = 0; i < 2000; i++) {
        uint8_t irq_status;

        result = read_register(reader, CommIrqReg, &irq_status);
        if (result != ESP_OK) return result;

        if (irq_status & 0x20) {
            received = true;
            break;
        }

        /* Timer interrupt means no response arrived in time. */
        if (irq_status & 0x01) {
            timer_expired = true;
            break;
        }

        if (irq_status & 0x10) {
            command_idle = true;
            break;
        }

        esp_rom_delay_us(50);
    }

    result = clear_register_bits(reader, BitFramingReg, 0x80);
    if (result != ESP_OK) return result;

    if (!received) {
        if (timer_expired || command_idle) {
            return ESP_ERR_NOT_FOUND;
        }

        return ESP_ERR_TIMEOUT;
    }

    uint8_t error_register;

    result = read_register(reader, ErrorReg, &error_register);
    if (result != ESP_OK) return result;

    if (error_register & 0x1B) {
        ESP_LOGW(
            TAG,
            "PICC exchange failed: command=0x%02X arg=0x%02X ErrorReg=0x%02X",
            send_data[0],
            send_length > 1 ? send_data[1] : 0,
            error_register
        );
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t fifo_count;

    result = read_register(reader, FIFOLevelReg, &fifo_count);
    if (result != ESP_OK) return result;

    if (fifo_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (fifo_count > RFID_MAX_FRAME) {
        ESP_LOGW(TAG, "Invalid FIFO response length: %u", fifo_count);
        return ESP_ERR_INVALID_RESPONSE;
    }

    result = read_fifo(reader, receive_data, fifo_count);
    if (result != ESP_OK) return result;

    *receive_length = fifo_count;

    uint8_t control;

    result = read_register(reader, ControlReg, &control);
    if (result != ESP_OK) return result;

    if (last_bits != NULL) {
        *last_bits = control & 0x07;
    }

    return ESP_OK;
}

/* ---------------------------------------------------------
 * WAKE UP CARD
 * --------------------------------------------------------- */

static esp_err_t wake_card(
    rfid_reader_t *reader,
    uint8_t *atqa,
    uint8_t *atqa_length
)
{
    uint8_t command = PICC_WUPA;
    uint8_t last_bits = 0;

    return transceive(
        reader,
        &command,
        1,
        0x07,
        atqa,
        atqa_length,
        &last_bits
    );
}

/* ---------------------------------------------------------
 * ANTICOLLISION AND CARD SELECTION
 * --------------------------------------------------------- */

static esp_err_t select_card_level(
    rfid_reader_t *reader,
    uint8_t select_code,
    uint8_t level_data[5],
    uint8_t *sak
)
{
    uint8_t anticollision_command[2] = {
        select_code,
        0x20
    };

    uint8_t response[RFID_MAX_FRAME];
    uint8_t response_length = 0;
    uint8_t last_bits = 0;

    esp_err_t result = transceive(
        reader,
        anticollision_command,
        2,
        0,
        response,
        &response_length,
        &last_bits
    );

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Anti-collision failed: %s",
                 esp_err_to_name(result));
        return result;
    }

    if (response_length != 5 || last_bits != 0) {
        ESP_LOGE(TAG, "Invalid anti-collision response: length=%u bits=%u",
                 response_length, last_bits);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Check Block Check Character. */
    uint8_t expected_bcc =
        response[0] ^ response[1] ^
        response[2] ^ response[3];

    if (expected_bcc != response[4]) {
        ESP_LOGE(TAG,
                 "BCC mismatch. Expected=0x%02X Received=0x%02X",
                 expected_bcc, response[4]);

        return ESP_ERR_INVALID_RESPONSE;
    }

    memcpy(level_data, response, 5);

    /* Build SELECT frame. */
    uint8_t select_frame[9];

    select_frame[0] = select_code;
    select_frame[1] = 0x70;

    memcpy(&select_frame[2], level_data, 5);

    uint8_t crc_low;
    uint8_t crc_high;

    result = calculate_crc(
        reader,
        select_frame,
        7,
        &crc_low,
        &crc_high
    );

    if (result != ESP_OK) {
        return result;
    }

    select_frame[7] = crc_low;
    select_frame[8] = crc_high;

    result = transceive(
        reader,
        select_frame,
        9,
        0,
        response,
        &response_length,
        &last_bits
    );

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Card SELECT failed: %s",
                 esp_err_to_name(result));
        return result;
    }

    if (response_length != 3 || last_bits != 0) {
        ESP_LOGW(
            TAG,
            "Invalid SELECT response: length=%u bits=%u",
            response_length,
            last_bits
        );
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t expected_crc_low;
    uint8_t expected_crc_high;
    result = calculate_crc(
        reader,
        response,
        1,
        &expected_crc_low,
        &expected_crc_high
    );
    if (result != ESP_OK) {
        return result;
    }

    if (response[1] != expected_crc_low ||
        response[2] != expected_crc_high) {
        ESP_LOGW(
            TAG,
            "Invalid SELECT CRC: expected=%02X%02X received=%02X%02X",
            expected_crc_high,
            expected_crc_low,
            response[2],
            response[1]
        );
        return ESP_ERR_INVALID_RESPONSE;
    }

    *sak = response[0];

    return ESP_OK;
}

/* ---------------------------------------------------------
 * HALT CARD
 * --------------------------------------------------------- */

static void halt_card(rfid_reader_t *reader)
{
    uint8_t halt_frame[4] = {
        PICC_HLTA,
        0x00,
        0x00,
        0x00
    };

    uint8_t crc_low;
    uint8_t crc_high;

    if (calculate_crc(
            reader,
            halt_frame,
            2,
            &crc_low,
            &crc_high
        ) != ESP_OK) {
        return;
    }

    halt_frame[2] = crc_low;
    halt_frame[3] = crc_high;

    uint8_t response[RFID_MAX_FRAME];
    uint8_t response_length = 0;

    /*
     * A successful HALT normally produces no response.
     * The timeout is therefore expected.
     */
    (void)transceive(
        reader,
        halt_frame,
        4,
        0,
        response,
        &response_length,
        NULL
    );
}

/* ---------------------------------------------------------
 * READ UID
 *
 * Supports 4-byte, 7-byte and 10-byte UIDs.
 * --------------------------------------------------------- */

esp_err_t rfid_read_uid(
    rfid_reader_t *reader,
    system_uid_t *uid
)
{
    if (reader == NULL || uid == NULL || !reader->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(uid, 0, sizeof(*uid));

    uint8_t atqa[RFID_MAX_FRAME];
    uint8_t atqa_length = 0;

    esp_err_t result = wake_card(
        reader,
        atqa,
        &atqa_length
    );

    if (result != ESP_OK) {
        if (result != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Card wake-up (WUPA) failed: %s",
                     esp_err_to_name(result));
        }
        return result;
    }

    if (atqa_length != 2) {
        ESP_LOGW(TAG, "Invalid WUPA response length: %u", atqa_length);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint8_t select_codes[3] = {
        PICC_SEL_CL1,
        PICC_SEL_CL2,
        PICC_SEL_CL3
    };

    for (int level = 0; level < 3; level++) {
        uint8_t level_data[5];
        uint8_t sak = 0;

        result = select_card_level(
            reader,
            select_codes[level],
            level_data,
            &sak
        );

        if (result != ESP_OK) {
            memset(uid, 0, sizeof(*uid));
            return result;
        }

        bool more_levels = (sak & 0x04) != 0;
        bool has_cascade_tag = level_data[0] == PICC_CT;
        if (more_levels != has_cascade_tag) {
            ESP_LOGW(
                TAG,
                "Cascade mismatch: SAK=0x%02X UID0=0x%02X level=%d",
                sak,
                level_data[0],
                level + 1
            );
            memset(uid, 0, sizeof(*uid));
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (has_cascade_tag) {
            if (uid->length > SYSTEM_UID_MAX_BYTES - 3) {
                ESP_LOGW(TAG, "UID exceeds supported length at cascade level %d",
                         level + 1);
                memset(uid, 0, sizeof(*uid));
                return ESP_ERR_INVALID_SIZE;
            }

            memcpy(
                &uid->bytes[uid->length],
                &level_data[1],
                3
            );

            uid->length += 3;
        } else {
            if (uid->length > SYSTEM_UID_MAX_BYTES - 4) {
                ESP_LOGW(TAG, "UID exceeds supported length at level %d",
                         level + 1);
                memset(uid, 0, sizeof(*uid));
                return ESP_ERR_INVALID_SIZE;
            }

            memcpy(
                &uid->bytes[uid->length],
                level_data,
                4
            );

            uid->length += 4;
        }

        if (!more_levels) {
            halt_card(reader);

            return ESP_OK;
        }
    }

    memset(uid, 0, sizeof(*uid));
    ESP_LOGW(TAG, "UID selection exceeded the three cascade levels");
    return ESP_ERR_INVALID_RESPONSE;
}

/* ---------------------------------------------------------
 * INITIALIZE ONE MFRC522 READER
 * --------------------------------------------------------- */

esp_err_t rfid_reader_init(
    rfid_reader_t *reader,
    gpio_num_t cs_gpio,
    gpio_num_t reset_gpio
)
{
    if (reader == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(reader, 0, sizeof(*reader));

    reader->reset_gpio = reset_gpio;

    gpio_config_t reset_config = {
        .pin_bit_mask = (1ULL << reset_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t result = gpio_config(&reset_config);

    if (result != ESP_OK) {
        return result;
    }

    result = gpio_set_level(reset_gpio, 1);
    if (result != ESP_OK) return result;

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = RFID_SPI_SPEED,
        .mode = 0,
        .spics_io_num = cs_gpio,
        .queue_size = 1,
        .flags = 0
    };

    result = spi_bus_add_device(
        RFID_SPI_HOST,
        &device_config,
        &reader->spi
    );

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not add RFID SPI device: %s",
                 esp_err_to_name(result));
        return result;
    }

    /* Hardware reset. */
    gpio_set_level(reset_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_level(reset_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    result = write_register(
        reader,
        CommandReg,
        PCD_SOFTRESET
    );

    if (result != ESP_OK) return result;

    vTaskDelay(pdMS_TO_TICKS(50));

    /*
     * Timer configuration:
     * Prescaler = 0x3E
     * Reload = 1000 (0x03E8)
     *
     * This gives the card more time to respond than
     * the previous short reload value of 30.
     */
    result = write_register(reader, TModeReg, 0x8D);
    if (result != ESP_OK) return result;

    result = write_register(reader, TPrescalerReg, 0x3E);
    if (result != ESP_OK) return result;

    result = write_register(reader, TReloadRegH, 0x03);
    if (result != ESP_OK) return result;

    result = write_register(reader, TReloadRegL, 0xE8);
    if (result != ESP_OK) return result;

    result = write_register(reader, TxASKReg, 0x40);
    if (result != ESP_OK) return result;

    result = write_register(reader, ModeReg, 0x3D);
    if (result != ESP_OK) return result;

    uint8_t version = 0;

    result = read_register(reader, VersionReg, &version);

    if (result != ESP_OK) return result;

    ESP_LOGI(TAG, "MFRC522 version register: 0x%02X", version);

    if (version != 0x88 && version != 0x91 && version != 0x92) {
        ESP_LOGE(TAG, "Unexpected MFRC522 version: 0x%02X", version);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Enable the antenna. */
    uint8_t tx_control;

    result = read_register(reader, TxControlReg, &tx_control);
    if (result != ESP_OK) return result;

    if ((tx_control & 0x03) != 0x03) {
        result = write_register(
            reader,
            TxControlReg,
            tx_control | 0x03
        );

        if (result != ESP_OK) return result;
    }

    reader->initialized = true;

    ESP_LOGI(TAG, "RFID reader initialized. CS=%d RESET=%d",
             (int)cs_gpio, (int)reset_gpio);

    return ESP_OK;
}