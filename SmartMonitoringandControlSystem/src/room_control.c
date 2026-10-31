#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "room_control.h"
#include "room_control_core.h"

#include "controller.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* A is outside and B is inside each room doorway. */
#define ROOM_1_A_GPIO     GPIO_NUM_34
#define ROOM_1_B_GPIO     GPIO_NUM_35
#define ROOM_1_LIGHT_GPIO GPIO_NUM_27
#define ROOM_2_A_GPIO     GPIO_NUM_36
#define ROOM_2_B_GPIO     GPIO_NUM_39
#define ROOM_2_LIGHT_GPIO GPIO_NUM_14

#define ROOM_SAMPLE_PERIOD_MS 25U
#define ROOM_INPUT_FILTER_MS  50U
#define ROOM_TASK_STACK_SIZE  4096U
#define ROOM_TASK_PRIORITY    3U
#define ROOM_COMMAND_QUEUE_LENGTH 4U

typedef struct {
    gpio_num_t gpio;
    bool raw_active;
    bool stable_active;
    int64_t raw_changed_ms;
} room_sensor_t;

typedef struct {
    const char *name;
    uint8_t id;
    room_sensor_t a;
    room_sensor_t b;
    gpio_num_t light_gpio;
    bool light_on;
    bool faulted;
    bool report_pending;
    bool report_error_logged;
    room_counter_t counter;
    system_event_t pending_event;
} room_control_t;

typedef struct {
    uint8_t room_id;
} room_command_t;

static const char *TAG = "room_control";
static bool started;
static QueueHandle_t command_queue;

static room_control_t rooms[] = {
    {
        .name = "room_1", .id = 1,
        .a = {.gpio = ROOM_1_A_GPIO}, .b = {.gpio = ROOM_1_B_GPIO},
        .light_gpio = ROOM_1_LIGHT_GPIO,
    },
    {
        .name = "room_2", .id = 2,
        .a = {.gpio = ROOM_2_A_GPIO}, .b = {.gpio = ROOM_2_B_GPIO},
        .light_gpio = ROOM_2_LIGHT_GPIO,
    },
};

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static room_control_t *room_by_id(uint8_t room_id)
{
    for (size_t i = 0; i < sizeof(rooms) / sizeof(rooms[0]); ++i) {
        if (rooms[i].id == room_id) return &rooms[i];
    }
    return NULL;
}

/* A failed post retries the exact same logical event ID. */
static void room_mark_for_report(room_control_t *room)
{
    room->pending_event = (system_event_t){
        .id = controller_next_event_id(),
        .type = EVENT_ROOM_STATE,
        .data.room = {
            .room_id = room->id,
            .occupancy = room->counter.occupancy,
            .light_on = room->light_on,
        },
    };
    room->report_pending = true;
    room->report_error_logged = false;
}

static void room_mark_fault(room_control_t *room, esp_err_t error)
{
    ESP_LOGE(TAG, "%s output fault: %s", room->name, esp_err_to_name(error));
    room->faulted = true;
    room->counter.occupancy = ROOM_UNKNOWN;
    room_mark_for_report(room);
}

/* UNKNOWN deliberately leaves the light on: reset/ A fault must not darken a room. */
static void room_apply_light(room_control_t *room)
{
    bool wanted_on = room->counter.occupancy != ROOM_EMPTY;
    if (room->light_on == wanted_on || room->faulted) return;

    esp_err_t error = gpio_set_level(room->light_gpio, wanted_on ? 1 : 0);
    if (error != ESP_OK) {
        room_mark_fault(room, error);
        return;
    }

    room->light_on = wanted_on;
    room_mark_for_report(room);
    ESP_LOGI(TAG, "Room %u light %s", room->id, wanted_on ? "ON" : "OFF");
}

static void room_try_report(room_control_t *room)
{
    if (!room->report_pending) return;

    esp_err_t error = controller_post_event(&room->pending_event, pdMS_TO_TICKS(10));
    if (error == ESP_OK) {
        room->report_pending = false;
        room->report_error_logged = false;
    } else if (!room->report_error_logged) {
        ESP_LOGW(TAG, "Room %u report queued for retry: %s",
                 room->id, esp_err_to_name(error));
        room->report_error_logged = true;
    }
}

static bool sensor_update(room_sensor_t *sensor, int64_t now)
{
    bool raw_active = gpio_get_level(sensor->gpio) != 0;
    if (raw_active != sensor->raw_active) {
        sensor->raw_active = raw_active;
        sensor->raw_changed_ms = now;
    }

    if (sensor->raw_active != sensor->stable_active &&
        now - sensor->raw_changed_ms >= ROOM_INPUT_FILTER_MS) {
        sensor->stable_active = sensor->raw_active;
        return sensor->stable_active; /* Rising edge only. */
    }
    return false;
}

static void room_process(room_control_t *room, int64_t now)
{
    bool a_rising = sensor_update(&room->a, now);
    bool b_rising = sensor_update(&room->b, now);

    if (!room->faulted) {
        room_counter_result_t result = room_counter_process(
            &room->counter, a_rising, b_rising,
            room->a.stable_active, room->b.stable_active, (uint64_t)now
        );
        if (result.ambiguous) {
            ESP_LOGW(TAG, "Room %u passage ambiguous; physical reconciliation required", room->id);
        }
        if (result.occupancy_changed) room_mark_for_report(room);
        room_apply_light(room);
    }

    room_try_report(room);
}

static void room_reconcile_empty(room_control_t *room)
{
    if (room->a.stable_active || room->b.stable_active) {
        ESP_LOGW(TAG, "Room %u cannot reconcile while a doorway beam is active", room->id);
        return;
    }

    room_counter_result_t result = room_counter_reconcile_empty(&room->counter);
    room->faulted = false;
    if (result.occupancy_changed || result.count_changed) room_mark_for_report(room);
    room_apply_light(room);
    ESP_LOGI(TAG, "Room %u reconciled as physically empty", room->id);
}

static void room_process_commands(void)
{
    room_command_t command;
    while (xQueueReceive(command_queue, &command, 0) == pdTRUE) {
        room_control_t *room = room_by_id(command.room_id);
        if (room) room_reconcile_empty(room);
    }
}

static void room_control_task(void *argument)
{
    (void)argument;
    TickType_t wake_time = xTaskGetTickCount();

    for (;;) {
        room_process_commands();
        int64_t now = now_ms();
        for (size_t i = 0; i < sizeof(rooms) / sizeof(rooms[0]); ++i) {
            room_process(&rooms[i], now);
        }
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(ROOM_SAMPLE_PERIOD_MS));
    }
}

static esp_err_t sensor_configure(room_sensor_t *sensor, int64_t now)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << sensor->gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t error = gpio_config(&config);
    if (error == ESP_OK) {
        sensor->raw_active = gpio_get_level(sensor->gpio) != 0;
        sensor->stable_active = sensor->raw_active;
        sensor->raw_changed_ms = now;
    }
    return error;
}

static esp_err_t room_configure(room_control_t *room)
{
    int64_t now = now_ms();
    esp_err_t error = sensor_configure(&room->a, now);
    if (error != ESP_OK) return error;
    error = sensor_configure(&room->b, now);
    if (error != ESP_OK) return error;

    gpio_config_t light_config = {
        .pin_bit_mask = 1ULL << room->light_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    error = gpio_config(&light_config);
    if (error != ESP_OK) return error;

    /* Unknown after reset: light ON until authorized physical reconciliation. */
    error = gpio_set_level(room->light_gpio, 1);
    if (error != ESP_OK) return error;
    room->light_on = true;
    room->faulted = false;
    room->report_pending = false;
    room->report_error_logged = false;
    room_counter_init(&room->counter);
    room_mark_for_report(room);
    return ESP_OK;
}

esp_err_t room_control_start(void)
{
    if (started) return ESP_ERR_INVALID_STATE;

    command_queue = xQueueCreate(ROOM_COMMAND_QUEUE_LENGTH, sizeof(room_command_t));
    if (!command_queue) return ESP_ERR_NO_MEM;

    for (size_t i = 0; i < sizeof(rooms) / sizeof(rooms[0]); ++i) {
        esp_err_t error = room_configure(&rooms[i]);
        if (error != ESP_OK) {
            vQueueDelete(command_queue);
            command_queue = NULL;
            return error;
        }
    }

    if (xTaskCreate(room_control_task, "room_control", ROOM_TASK_STACK_SIZE,
                    NULL, ROOM_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(command_queue);
        command_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    started = true;
    ESP_LOGI(TAG, "Direction counters started: R1 A=%d B=%d light=%d; R2 A=%d B=%d light=%d",
             ROOM_1_A_GPIO, ROOM_1_B_GPIO, ROOM_1_LIGHT_GPIO,
             ROOM_2_A_GPIO, ROOM_2_B_GPIO, ROOM_2_LIGHT_GPIO);
    return ESP_OK;
}

esp_err_t room_control_reconcile_empty(uint8_t room_id, TickType_t timeout)
{
    if (!started) return ESP_ERR_INVALID_STATE;
    if (!room_by_id(room_id)) return ESP_ERR_INVALID_ARG;
    room_command_t command = {.room_id = room_id};
    return xQueueSend(command_queue, &command, timeout) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
