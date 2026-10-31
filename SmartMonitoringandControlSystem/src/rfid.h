
#ifndef RFID_H
#define RFID_H

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "system_types.h"

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t reset_gpio;
    bool initialized;
} rfid_reader_t;

/* Initialize the shared SPI bus once. */
esp_err_t rfid_spi_bus_init(void);

/* Initialize one MFRC522 reader on the SPI bus. */
esp_err_t rfid_reader_init(
    rfid_reader_t *reader,
    gpio_num_t cs_gpio,
    gpio_num_t reset_gpio
);

/* Read a four-byte card UID. */
esp_err_t rfid_read_uid(
    rfid_reader_t *reader,
    system_uid_t *uid
);

#endif
