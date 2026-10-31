#ifndef ROOM_CONTROL_H
#define ROOM_CONTROL_H

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/*
 * Starts the independent Room 1 and Room 2 sensor/light state machines.
 * controller_start() must have completed first because this module publishes
 * EVENT_ROOM_STATE updates through the controller queue.
 */
esp_err_t room_control_start(void);

/* Apply after an authenticated physical inspection establishes room emptiness. */
esp_err_t room_control_reconcile_empty(uint8_t room_id, TickType_t timeout);

#endif
