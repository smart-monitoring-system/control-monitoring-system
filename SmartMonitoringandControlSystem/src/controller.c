#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_config.h"
#include "controller.h"
#include "controller_core.h"
#include "integration.h"

typedef struct { bool demo_schedule_command; bool open; system_event_t event; } queued_event_t;
static controller_core_t core;
static QueueHandle_t queue;
static SemaphoreHandle_t snapshot_lock;
static portMUX_TYPE id_lock = portMUX_INITIALIZER_UNLOCKED;
static system_event_id_t next_id;
static bool started;
static const char *TAG = "controller";
#if APP_ROLE1_DEMO
static bool demo_schedule_open = true; /* Written only by the controller task. */
#endif

system_event_id_t controller_next_event_id(void)
{
    portENTER_CRITICAL(&id_lock);
    if (++next_id == 0) ++next_id;
    system_event_id_t id = next_id;
    portEXIT_CRITICAL(&id_lock);
    return id;
}
esp_err_t controller_post_event(const system_event_t *e, TickType_t timeout)
{
    if (!started) return ESP_ERR_INVALID_STATE;
    if (!e || !e->id) return ESP_ERR_INVALID_ARG;
    queued_event_t item = {.event = *e};
    return xQueueSend(queue, &item, timeout) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t controller_get_snapshot(system_snapshot_t *out, TickType_t timeout)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!started) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(snapshot_lock, timeout) != pdTRUE) return ESP_ERR_TIMEOUT;
    *out = core.snapshot;
    xSemaphoreGive(snapshot_lock);
    return ESP_OK;
}
esp_err_t controller_demo_set_schedule(bool open, TickType_t timeout)
{
#if APP_ROLE1_DEMO
    if (!started) return ESP_ERR_INVALID_STATE;
    queued_event_t item = {.demo_schedule_command = true, .open = open};
    return xQueueSend(queue, &item, timeout) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
#else
    (void)open; (void)timeout;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
static access_facts_t resolve_access(const system_event_t *e)
{
    access_facts_t facts = {0};
    const system_uid_t *uid = &e->data.scan.uid;
    if (uid->length != 4 && uid->length != 7 && uid->length != 10) return facts;
#if APP_ROLE1_DEMO
    /* Explicit synthetic registry. Normal firmware compiles this out entirely. */
    const uint8_t alice[] = {1, 2, 3, 4}, brian[] = {0x11, 0x22, 0x33, 0x44};
    const uint8_t carol[] = {0x55, 0x66, 0x77, 0x88};
    facts.identity_ready = true;
    if (uid->length == 4) {
        if (!memcmp(uid->bytes, alice, 4)) facts.user_id = 1;
        if (!memcmp(uid->bytes, brian, 4)) facts.user_id = 2;
        if (!memcmp(uid->bytes, carol, 4)) facts.user_id = 3;
    }
    facts.identity_known = facts.user_id != 0;
    facts.schedule_ready = true;
    facts.schedule_open = demo_schedule_open;
#else
    facts.identity_ready = rfid_lookup_user(uid, &facts.user_id, &facts.identity_known) == ESP_OK;
    facts.schedule_ready = schedule_access_allowed(e->data.scan.direction, &facts.schedule_open) == ESP_OK;
#endif
    return facts;
}
static esp_err_t set_door(system_direction_t d, bool open)
{
#if APP_ROLE1_DEMO
    ESP_LOGI(TAG, "DEMO actuator: %s %s (no physical servo)",
             d == DIRECTION_ENTRY ? "ENTRY" : "EXIT", open ? "OPEN" : "CLOSE");
    return ESP_OK;
#else
    return door_set_open(d, open);
#endif
}
static void publish_effect(system_event_id_t id, uint64_t now, controller_effect_t effect,
    const system_event_t *source)
{
    /* Do not hold the snapshot mutex while calling external modules. */
    for (unsigned d = 0; d < 2; ++d) {
        if (effect.close_mask & (1U << d)) {
            esp_err_t err = set_door((system_direction_t)d, false);
            if (err != ESP_OK) ESP_LOGE(TAG, "door close failed: %s", esp_err_to_name(err));
        }
        if (effect.expired_mask & (1U << d)) ESP_LOGW(TAG, "passage timeout on lane %u; occupancy unchanged", d);
        if (effect.open_mask & (1U << d)) {
            esp_err_t err = set_door((system_direction_t)d, true);
            if (err != ESP_OK) {
                xSemaphoreTake(snapshot_lock, portMAX_DELAY);
                controller_core_cancel(&core, (system_direction_t)d);
                xSemaphoreGive(snapshot_lock);
                effect.result = RESULT_DOOR_UNAVAILABLE;
                ESP_LOGE(TAG, "door request failed: %s; pending transaction cancelled", esp_err_to_name(err));
            }
        }
    }
    if (effect.result == RESULT_IDLE) return;
    system_snapshot_t snapshot;
    if (controller_get_snapshot(&snapshot, pdMS_TO_TICKS(100)) != ESP_OK) return;
    ESP_LOGI(TAG, "event=%" PRIu32 " result=%s user=%" PRIu32 " inside=%u",
             id, system_result_name(effect.result), effect.user_id, (unsigned)snapshot.building.count);
    system_audit_record_t audit = {.event_id = id, .uptime_ms = now,
        .result = effect.result, .user_id = effect.user_id,
        .inside_count = snapshot.building.count, .has_source_event = source != NULL};
    if (source) audit.source_event = *source;
    esp_err_t err = storage_queue_record(&audit);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) ESP_LOGE(TAG, "audit queue failed: %s", esp_err_to_name(err));
}
static void controller_task(void *arg)
{
    (void)arg;
    queued_event_t item;
    for (;;) {
        bool received = xQueueReceive(queue, &item, pdMS_TO_TICKS(100)) == pdTRUE;
        uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
        controller_effect_t effect;
        system_event_id_t id = 0;
#if APP_ROLE1_DEMO
        if (received && item.demo_schedule_command) {
            demo_schedule_open = item.open;
            ESP_LOGW(TAG, "DEMO schedule now %s", item.open ? "OPEN" : "CLOSED");
            /* Revoke outstanding approvals when the demonstration schedule closes. */
            if (!item.open) {
                xSemaphoreTake(snapshot_lock, portMAX_DELAY);
                system_event_t revoke = {.id = controller_next_event_id(), .type = EVENT_REVOKE_PENDING};
                controller_effect_t revoked = controller_core_process(&core, &revoke, NULL, now);
                xSemaphoreGive(snapshot_lock);
                publish_effect(revoke.id, now, revoked, &revoke);
            }
            continue;
        }
#endif
        access_facts_t facts = {0};
        if (received && item.event.type == EVENT_ACCESS_SCAN) facts = resolve_access(&item.event);
        xSemaphoreTake(snapshot_lock, portMAX_DELAY);
        if (received) {
            id = item.event.id;
            effect = controller_core_process(&core, &item.event, &facts, now);
        } else effect = controller_core_tick(&core, now);
        xSemaphoreGive(snapshot_lock);
        publish_effect(id, now, effect, received ? &item.event : NULL);
    }
}
esp_err_t controller_start(void)
{
    if (started) return ESP_ERR_INVALID_STATE;
    queue = xQueueCreate(SYSTEM_EVENT_QUEUE_LENGTH, sizeof(queued_event_t));
    snapshot_lock = xSemaphoreCreateMutex();
    if (!queue || !snapshot_lock) {
        if (queue) vQueueDelete(queue);
        if (snapshot_lock) vSemaphoreDelete(snapshot_lock);
        return ESP_ERR_NO_MEM;
    }
    controller_core_init(&core);
    started = true;
    if (xTaskCreate(controller_task, "controller", APP_CONTROLLER_STACK_SIZE, NULL,
                    APP_CONTROLLER_PRIORITY, NULL) != pdPASS) {
        started = false;
        vQueueDelete(queue); vSemaphoreDelete(snapshot_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
