#include <stddef.h>
#include "schedule.h"
#include "config.h"
#include "integration.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MINUTES_PER_DAY 1440
#define MICROSECONDS_PER_MINUTE 60000000LL
#define LOCK_WAIT_TICKS pdMS_TO_TICKS(100)

/* Our hand-set clock. Times are Kampala local time (East Africa Time, UTC+3). */
static bool clock_is_set = false;
static int minutes_when_set = 0;
static int64_t uptime_when_set_us = 0;

/* The key to the clock. NULL until schedule_init() has run. */
static SemaphoreHandle_t clock_mutex = NULL;

esp_err_t schedule_init(void)
{
    if (clock_mutex != NULL) {
        return ESP_OK;   /* already created */
    }
    clock_mutex = xSemaphoreCreateMutex();
    if (clock_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* Is this hour inside the opening hours? Open at the start hour, closed at the end hour. */
bool schedule_is_open(int hour)
{
    int open_hour = 0;
    int close_hour = 0;

    if (config_get_hours(&open_hour, &close_hour) != ESP_OK) {
        return false;   /* cannot read the settings: stay closed */
    }

    if (hour >= open_hour && hour < close_hour) {
        return true;
    }
    return false;
}

esp_err_t schedule_set_time(int hour, int minute)
{
    if (hour < 0 || hour > 23) {
        return ESP_ERR_INVALID_ARG;
    }
    if (minute < 0 || minute > 59) {
        return ESP_ERR_INVALID_ARG;
    }
    if (clock_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int64_t now_us = esp_timer_get_time();

    /* Take the key, write all three values together, hang the key back */
    if (xSemaphoreTake(clock_mutex, LOCK_WAIT_TICKS) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    minutes_when_set = (hour * 60) + minute;
    uptime_when_set_us = now_us;
    clock_is_set = true;
    xSemaphoreGive(clock_mutex);

    return ESP_OK;
}

esp_err_t schedule_get_time(int *hour, int *minute)
{
    if (hour == NULL || minute == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (clock_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Take the key, copy the three clock values out together, hang the key back */
    bool is_set = false;
    int start_minutes = 0;
    int64_t start_us = 0;

    if (xSemaphoreTake(clock_mutex, LOCK_WAIT_TICKS) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    is_set = clock_is_set;
    start_minutes = minutes_when_set;
    start_us = uptime_when_set_us;
    xSemaphoreGive(clock_mutex);

    if (!is_set) {
        return ESP_ERR_INVALID_STATE;   /* the clock was never set */
    }

    /* Time now = the time we were given + how long has passed since */
    int64_t passed_us = esp_timer_get_time() - start_us;
    int64_t passed_minutes = passed_us / MICROSECONDS_PER_MINUTE;
    int minutes_now = (int)((start_minutes + passed_minutes) % MINUTES_PER_DAY);

    *hour = minutes_now / 60;
    *minute = minutes_now % 60;
    return ESP_OK;
}

bool schedule_time_is_set(void)
{
    int hour = 0;
    int minute = 0;
    return schedule_get_time(&hour, &minute) == ESP_OK;
}

/* The function the controller calls on every scan, for ENTRY and for EXIT */
esp_err_t schedule_access_allowed(system_direction_t direction, bool *allowed)
{
    if (allowed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *allowed = false;   /* any problem below means "denied" */

    if (direction != DIRECTION_ENTRY && direction != DIRECTION_EXIT) {
        return ESP_ERR_INVALID_ARG;
    }

    int hour = 0;
    int minute = 0;
    esp_err_t result = schedule_get_time(&hour, &minute);
    if (result != ESP_OK) {
        return result;   /* clock not set or unavailable: report an error, never guess */
    }

    *allowed = schedule_is_open(hour);
    return ESP_OK;
}