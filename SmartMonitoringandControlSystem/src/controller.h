#ifndef CONTROLLER_H
#define CONTROLLER_H
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "system_types.h"
esp_err_t controller_start(void);
/* Task-context API; do not call this from an ISR. Events are copied into a queue. */
esp_err_t controller_post_event(const system_event_t *event, TickType_t timeout);
system_event_id_t controller_next_event_id(void);
/* A bounded, consistent COPY. Never return pointers to the mutable register. */
esp_err_t controller_get_snapshot(system_snapshot_t *out, TickType_t timeout);
/* Synthetic schedule toggle, disabled with ESP_ERR_NOT_SUPPORTED outside demo. */
esp_err_t controller_demo_set_schedule(bool open, TickType_t timeout);
#endif
