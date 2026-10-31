#include <stddef.h>
#include "config.h"
#include "auth.h"
#include "schedule.h"
#include "controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define LOCK_WAIT_TICKS pdMS_TO_TICKS(100)
#define WATCH_INTERVAL_MS 1000
#define WATCH_TASK_STACK 3072
#define WATCH_TASK_PRIORITY 2

/* The settings. The building opens at 7 and closes at 22. */
static int saved_open_hour = 7;
static int saved_close_hour = 22;

/* The key to the settings. NULL until management_init() has run. */
static SemaphoreHandle_t settings_mutex = NULL;

esp_err_t management_init(void)
{
    esp_err_t err = schedule_init();
    if (err != ESP_OK) {
        return err;
    }

    if (settings_mutex == NULL) {
        settings_mutex = xSemaphoreCreateMutex();
        if (settings_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t config_get_hours(int *open_hour, int *close_hour)
{
    if (open_hour == NULL || close_hour == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (settings_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Take the key, copy both hours together, hang the key back */
    if (xSemaphoreTake(settings_mutex, LOCK_WAIT_TICKS) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *open_hour = saved_open_hour;
    *close_hour = saved_close_hour;
    xSemaphoreGive(settings_mutex);

    return ESP_OK;
}

/* Tell the controller to cancel everyone who was approved but has not passed yet */
static esp_err_t cancel_pending_approvals(void)
{
    system_event_t revoke = {0};
    revoke.id = controller_next_event_id();
    revoke.type = EVENT_REVOKE_PENDING;
    return controller_post_event(&revoke, pdMS_TO_TICKS(100));
}

esp_err_t config_set_hours(int open_hour, int close_hour)
{
    /* Only a logged-in admin may change the schedule */
    if (!auth_is_logged_in()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (settings_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Check the new hours before changing anything.
     * Overnight schedules are not supported, so the opening hour must be earlier. */
    if (open_hour < 0 || open_hour > 23) {
        return ESP_ERR_INVALID_ARG;
    }
    if (close_hour < 1 || close_hour > 24) {
        return ESP_ERR_INVALID_ARG;
    }
    if (open_hour >= close_hour) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Take the key, remember the old hours, write both new hours, hang the key back */
    int old_open = 0;
    int old_close = 0;

    if (xSemaphoreTake(settings_mutex, LOCK_WAIT_TICKS) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    old_open = saved_open_hour;
    old_close = saved_close_hour;
    saved_open_hour = open_hour;
    saved_close_hour = close_hour;
    xSemaphoreGive(settings_mutex);

    /* Cancel old approvals. If that fails, put the old hours back. */
    esp_err_t err = cancel_pending_approvals();
    if (err != ESP_OK) {
        /* Waiting forever is safe here: nobody keeps this key for long */
        xSemaphoreTake(settings_mutex, portMAX_DELAY);
        saved_open_hour = old_open;
        saved_close_hour = old_close;
        xSemaphoreGive(settings_mutex);
        return err;
    }

    auth_note_activity();
    return ESP_OK;
}

esp_err_t config_set_time(int hour, int minute)
{
    if (!auth_is_logged_in()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = schedule_set_time(hour, minute);
    if (err != ESP_OK) {
        return err;
    }

    auth_note_activity();
    return cancel_pending_approvals();
}

static bool watcher_started = false;

/* Every second: if the building was open and is now closed, cancel pending approvals */
static void watcher_task(void *arg)
{
    (void)arg;
    bool know_state = false;   /* do we know if it was open last time? */
    bool was_open = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(WATCH_INTERVAL_MS));

        int hour = 0;
        int minute = 0;
        if (schedule_get_time(&hour, &minute) != ESP_OK) {
            know_state = false;   /* clock not set: nothing to compare */
            continue;
        }

        bool is_open = schedule_is_open(hour);

        if (know_state && was_open && !is_open) {
            if (cancel_pending_approvals() != ESP_OK) {
                continue;   /* try again on the next check */
            }
        }

        was_open = is_open;
        know_state = true;
    }
}

esp_err_t config_start_watcher(void)
{
    if (watcher_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(watcher_task, "sched_watch", WATCH_TASK_STACK, NULL,
                    WATCH_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    watcher_started = true;
    return ESP_OK;
}
