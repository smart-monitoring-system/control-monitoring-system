#include <stddef.h>
#include "schedule.h"
#include "integration.h"
#include "esp_timer.h"

#define MINUTES_PER_DAY 1440
#define MICROSECONDS_PER_MINUTE 60000000LL

/* The opening hours: open at 7, closed from 22 onwards */
static int open_hour = 7;
static int close_hour = 22;

/* Our hand-set clock */
static bool clock_is_set = false;
static int minutes_when_set = 0;
static int64_t uptime_when_set_us = 0;

/* Pure yes/no check: is this hour inside the opening hours? */
bool schedule_is_open(int hour)
{
    if (hour >= open_hour && hour < close_hour) {
        return true;
    }
    return false;
}

/* Set the clock: remember the time given, and remember how long the board has been on */
esp_err_t schedule_set_time(int hour, int minute)
{
    if (hour < 0 || hour > 23) {
        return ESP_ERR_INVALID_ARG;
    }
    if (minute < 0 || minute > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    minutes_when_set = (hour * 60) + minute;
    uptime_when_set_us = esp_timer_get_time();
    clock_is_set = true;
    return ESP_OK;
}

bool schedule_time_is_set(void)
{
    return clock_is_set;
}

/* Work out the time now = time we were given + how long has passed since */
esp_err_t schedule_get_time(int *hour, int *minute)
{
    if (hour == NULL || minute == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!clock_is_set) {
        return ESP_ERR_INVALID_STATE;
    }

    int64_t passed_us = esp_timer_get_time() - uptime_when_set_us;
    int64_t passed_minutes = passed_us / MICROSECONDS_PER_MINUTE;
    int minutes_now = (int)((minutes_when_set + passed_minutes) % MINUTES_PER_DAY);

    *hour = minutes_now / 60;
    *minute = minutes_now % 60;
    return ESP_OK;
}

/* The function the leader's controller calls on every scan */
esp_err_t schedule_access_allowed(system_direction_t direction, bool *allowed)
{
    if (allowed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *allowed = false;

    if (direction != DIRECTION_ENTRY && direction != DIRECTION_EXIT) {
        return ESP_ERR_INVALID_ARG;
    }

    int hour = 0;
    int minute = 0;
    esp_err_t result = schedule_get_time(&hour, &minute);
    if (result != ESP_OK) {
        return result;   /* clock not set: report an error, never guess */
    }

    *allowed = schedule_is_open(hour);
    return ESP_OK;
}