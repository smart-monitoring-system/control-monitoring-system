#ifndef SERIAL_CLI_H
#define SERIAL_CLI_H

#include "esp_err.h"

/* Starts the management menu on the serial port. Call once at startup. */
esp_err_t serial_cli_start(void);

#endif