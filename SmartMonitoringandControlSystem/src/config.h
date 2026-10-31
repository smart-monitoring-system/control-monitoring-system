#ifndef CONFIG_H
#define CONFIG_H

#include "esp_err.h"

/* Creates the locks used by the management modules. Call once at startup,
 * before the controller starts. */
esp_err_t management_init(void);

/* Read the opening hours. Anyone may read them, at any time of day. */
esp_err_t config_get_hours(int *open_hour, int *close_hour);

/* Change the opening hours. An admin must be logged in.
 * ESP_OK = changed.
 * ESP_ERR_INVALID_STATE = nobody is logged in (or startup was not done).
 * ESP_ERR_INVALID_ARG = hours not acceptable (nothing changed).
 * Any other error = the controller could not be told, so nothing was changed. */
esp_err_t config_set_hours(int open_hour, int close_hour);

/* Set the clock. An admin must be logged in. Same return values as above,
 * except that if the controller could not be told, the clock HAS been set. */
esp_err_t config_set_time(int hour, int minute);

/* Starts a small background check that cancels pending approvals when the
 * building closes by itself. Call once, after controller_start(). */
esp_err_t config_start_watcher(void);

#endif