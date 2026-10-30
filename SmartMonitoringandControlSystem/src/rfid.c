#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "rfid.h"

#define RFID_TAG "PERSON_2_RFID"

/* Shared SPI connections */
#define RFID_SCK    GPIO_NUM_18
#define RFID_MISO   GPIO_NUM_19
#define RFID_MOSI   GPIO_NUM_23
#define RFID_RST    GPIO_NUM_22

/* Each reader has its own chip-select pin */
#define ENTRY_CS    GPIO_NUM_5
#define EXIT_CS     GPIO_NUM_17

#define RFID_SPI_HOST SPI2_HOST

/* MFRC522 registers */
#define CommandReg       0x01
#define CommIrqReg       0x04
#define ErrorReg         0x06
#define FIFODataReg      0x09
#define FIFOLevelReg     0x0A
#define ControlReg       0x0C
#define BitFramingReg    0x0D
#define ModeReg          0x11
#define TxControlReg     0x14
#define TxASKReg         0x15
#define TModeReg         0x2A
#define TPrescalerReg    0x2B
#define TReloadRegH      0x2C
#define TReloadRegL      0x2D
#define DivIrqReg        0x05
#define CRCResultRegH    0x21
#define CRCResultRegL    0x22

/* MFRC522 commands */
#define PCD_IDLE         0x00
#define PCD_TRANSCEIVE   0x0C
#define PCD_CALCCRC      0x03
#define PCD_SOFTRESET    0x0F

/* PICC commands */
#define PICC_WUPA        0x52
#define PICC_ANTICOLL    0x93
#define PICC_HALT        0x50

#define RFID_TIMEOUT_US  30000

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t cs_pin;
    door_direction_t direction;

    bool card_present;
    char previous_uid[9];
} rfid_reader_t;

static rfid_reader_t readers[2];

static const char *TAG = RFID_TAG;

/*
 * Demo registrations.
 * Replace these example UIDs with the actual UIDs printed
 * by your RFID readers.
 */
typedef struct {
    const char *uid;
    const char *name;
} registered_user_t;

static const registered_user_t registered_users[] = {
    {"DEADBEEF", "Alice"},
    {"11223344", "Bob"},
    {"A1B2C3D4", "Charlie"}
};

const char *lookup_user(const char *uid)
{
    if (uid == NULL) {
        return NULL;
    }

    for (unsigned int i = 0;
         i < sizeof(registered_users) / sizeof(registered_users[0]);
         i++) {

        if (strcasecmp(uid, registered_users[i].uid) == 0) {
            return registered_users[i].name;
        }
    }

    return NULL;
}

/* Write one MFRC522 register. */
static void write_register(rfid_reader_t *reader,
                           uint8_t reg,
                           uint8_t value)
{
    spi_transaction_t transaction = {0};

    transaction.flags = SPI_TRANS_USE_TXDATA;
    transaction.length = 16;

    transaction.tx_data[0] = (reg << 1) & 0x7E;
    transaction.tx_data[1] = value;

    ESP_ERROR_CHECK(spi_device_transmit(reader->spi, &transaction));
}

/* Read one MFRC522 register. */
static uint8_t read_register(rfid_reader_t *reader, uint8_t reg)
{
    spi_transaction_t transaction = {0};

    transaction.flags = SPI_TRANS_USE_TXDATA |
                        SPI_TRANS_USE_RXDATA;

    transaction.length = 16;

    transaction.tx_data[0] = ((reg << 1) & 0x7E) | 0x80;
    transaction.tx_data[1] = 0x00;

    ESP_ERROR_CHECK(spi_device_transmit(reader->spi, &transaction));

    return transaction.rx_data[1];
}

static void write_fifo(rfid_reader_t *reader,
                       const uint8_t *data,
                       uint8_t length)
{
    for (uint8_t i = 0; i < length; i++) {
        write_register(reader, FIFODataReg, data[i]);
    }
}

static void read_fifo(rfid_reader_t *reader,
                      uint8_t *data,
                      uint8_t length)
{
    for (uint8_t i = 0; i < length; i++) {
        data[i] = read_register(reader, FIFODataReg);
    }
}

/*
 * Send data to a card and receive its response.
 */
static esp_err_t transceive(rfid_reader_t *reader,
                            const uint8_t *tx,
                            uint8_t tx_length,
                            uint8_t tx_last_bits,
                            uint8_t *rx,
                            uint8_t *rx_length)
{
    write_register(reader, CommandReg, PCD_IDLE);
    write_register(reader, CommIrqReg, 0x7F);
    write_register(reader, FIFOLevelReg, 0x80);

    write_fifo(reader, tx, tx_length);

    write_register(reader, BitFramingReg, tx_last_bits);
    write_register(reader, CommandReg, PCD_TRANSCEIVE);

    write_register(reader, BitFramingReg, tx_last_bits | 0x80);

    int64_t start = esp_timer_get_time();

    uint8_t irq = 0;

    while ((esp_timer_get_time() - start) < RFID_TIMEOUT_US) {
        irq = read_register(reader, CommIrqReg);

        if (irq & 0x30) {
            break;
        }

        if (irq & 0x01) {
            return ESP_ERR_TIMEOUT;
        }
    }

    write_register(reader, BitFramingReg, tx_last_bits);

    if (!(irq & 0x30)) {
        return ESP_ERR_TIMEOUT;
    }

    if (read_register(reader, ErrorReg) & 0x1B) {
        return ESP_FAIL;
    }

    uint8_t count = read_register(reader, FIFOLevelReg) & 0x7F;

    if (count > 18) {
        return ESP_FAIL;
    }

    read_fifo(reader, rx, count);
    *rx_length = count;

    return ESP_OK;
}

/* Calculate CRC_A for the card HALT command. */
static void calculate_crc(rfid_reader_t *reader,
                          const uint8_t *data,
                          uint8_t length,
                          uint8_t *crc_low,
                          uint8_t *crc_high)
{
    write_register(reader, CommandReg, PCD_IDLE);
    write_register(reader, DivIrqReg, 0x04);
    write_register(reader, FIFOLevelReg, 0x80);

    write_fifo(reader, data, length);

    write_register(reader, CommandReg, PCD_CALCCRC);

    int64_t start = esp_timer_get_time();

    while ((esp_timer_get_time() - start) < 5000) {
        if (read_register(reader, DivIrqReg) & 0x04) {
            break;
        }
    }

    *crc_low = read_register(reader, CRCResultRegL);
    *crc_high = read_register(reader, CRCResultRegH);
}

/* Put the card into HALT so it can be detected again on the next poll. */
static void halt_card(rfid_reader_t *reader)
{
    uint8_t command[4] = {
        PICC_HALT, 0x00, 0x00, 0x00
    };

    calculate_crc(reader, command, 2,
                  &command[2], &command[3]);

    uint8_t response[18];
    uint8_t response_length = 0;

    /* A HALT command normally produces no response. */
    (void)transceive(reader, command, 4, 0,
                     response, &response_length);
}

/* Initialise one MFRC522 reader. */
static void initialise_reader(rfid_reader_t *reader)
{
    write_register(reader, CommandReg, PCD_SOFTRESET);
    vTaskDelay(pdMS_TO_TICKS(50));

    write_register(reader, TModeReg, 0x8D);
    write_register(reader, TPrescalerReg, 0x3E);
    write_register(reader, TReloadRegL, 30);
    write_register(reader, TReloadRegH, 0);

    write_register(reader, TxASKReg, 0x40);
    write_register(reader, ModeReg, 0x3D);

    uint8_t antenna = read_register(reader, TxControlReg);

    if (!(antenna & 0x03)) {
        write_register(reader, TxControlReg, antenna | 0x03);
    }

    ESP_LOGI(TAG, "Reader initialised on CS GPIO %d",
             reader->cs_pin);
}

/* Configure SPI and both RFID readers. */
void rfid_init(void)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = RFID_MOSI,
        .miso_io_num = RFID_MISO,
        .sclk_io_num = RFID_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64
    };

    esp_err_t result = spi_bus_initialize(
        RFID_SPI_HOST,
        &bus_config,
        SPI_DMA_CH_AUTO
    );

    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(result);
    }

    gpio_config_t reset_config = {
        .pin_bit_mask = (1ULL << RFID_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&reset_config));
    gpio_set_level(RFID_RST, 1);

    readers[0].cs_pin = ENTRY_CS;
    readers[0].direction = DOOR_ENTRY;

    readers[1].cs_pin = EXIT_CS;
    readers[1].direction = DOOR_EXIT;

    for (int i = 0; i < 2; i++) {
        spi_device_interface_config_t device_config = {
            .clock_speed_hz = 1000000,
            .mode = 0,
            .spics_io_num = readers[i].cs_pin,
            .queue_size = 1
        };

        ESP_ERROR_CHECK(spi_bus_add_device(
            RFID_SPI_HOST,
            &device_config,
            &readers[i].spi
        ));

        readers[i].card_present = false;
        memset(readers[i].previous_uid, 0,
               sizeof(readers[i].previous_uid));

        initialise_reader(&readers[i]);
    }

    ESP_LOGI(TAG, "Both RFID readers are ready.");
}

/*
 * Read a new card from one reader.
 * Returns true only when a card is newly detected.
 */
static bool read_card(rfid_reader_t *reader, char *uid_text)
{
    uint8_t command = PICC_WUPA;
    uint8_t response[18];
    uint8_t response_length = 0;

    if (transceive(reader, &command, 1, 0x07,
                   response, &response_length) != ESP_OK) {

        reader->card_present = false;
        reader->previous_uid[0] = '\0';

        return false;
    }

    /* Four-byte UID anti-collision command. */
    uint8_t anticollision[2] = {
        PICC_ANTICOLL, 0x20
    };

    if (transceive(reader, anticollision, 2, 0,
                   response, &response_length) != ESP_OK ||
        response_length < 5) {
        return false;
    }

    /* Check the UID block checksum. */
    if ((response[0] ^ response[1] ^
         response[2] ^ response[3]) != response[4]) {
        return false;
    }

    snprintf(uid_text, 9, "%02X%02X%02X%02X",
             response[0], response[1],
             response[2], response[3]);

    halt_card(reader);

    /* Suppress repeated scans while the same card remains present. */
    if (reader->card_present &&
        strcmp(reader->previous_uid, uid_text) == 0) {
        return false;
    }

    reader->card_present = true;

    strncpy(reader->previous_uid, uid_text,
            sizeof(reader->previous_uid) - 1);

    return true;
}

/* Check both readers and return the first new scan event. */
bool rfid_poll(rfid_scan_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    for (int i = 0; i < 2; i++) {
        char uid[9] = {0};

        if (read_card(&readers[i], uid)) {
            const char *name = lookup_user(uid);

            strncpy(event->uid, uid, sizeof(event->uid) - 1);

            if (name != NULL) {
                strncpy(event->participant, name,
                        sizeof(event->participant) - 1);
            } else {
                strncpy(event->participant, "UNKNOWN",
                        sizeof(event->participant) - 1);
            }

            event->direction = readers[i].direction;

            ESP_LOGI(TAG,
                     "SCAN: participant=%s, UID=%s, direction=%s",
                     event->participant,
                     event->uid,
                     event->direction == DOOR_ENTRY
                         ? "ENTRY" : "EXIT");

            return true;
        }
    }

    return false;
}