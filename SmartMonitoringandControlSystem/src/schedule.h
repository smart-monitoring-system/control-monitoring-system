#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdbool.h>
#include "esp_err.h"

/* Is the building open at this hour? (hour is 0 to 23) */
bool schedule_is_open(int hour);

/* Set the clock by hand, for example 8 and 30 for 08:30 */
esp_err_t schedule_set_time(int hour, int minute);

/* Has the clock been set since the board started? */
bool schedule_time_is_set(void);

/* Read the current time of day from our clock */
esp_err_t schedule_get_time(int *hour, int *minute);

#endif