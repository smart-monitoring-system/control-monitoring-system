#include "app_config.h"
#include "role1_demo.h"
#if APP_ROLE1_DEMO
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "controller.h"
static void post(system_event_t *event)
{
    ESP_ERROR_CHECK(controller_post_event(event, pdMS_TO_TICKS(1000)));
    vTaskDelay(pdMS_TO_TICKS(150));
}
static system_event_t scan(uint8_t who, system_direction_t direction)
{
    system_uid_t uid = {.length = 4};
    if (who == 1) uid = (system_uid_t){4, {1, 2, 3, 4}};
    else if (who == 2) uid = (system_uid_t){4, {0x11, 0x22, 0x33, 0x44}};
    else if (who == 3) uid = (system_uid_t){4, {0x55, 0x66, 0x77, 0x88}};
    else uid = (system_uid_t){4, {0xaa, 0xbb, 0xcc, 0xdd}};
    system_event_t event = {.id = controller_next_event_id(), .type = EVENT_ACCESS_SCAN,
        .data.scan = {.uid = uid, .direction = direction}};
    post(&event);
    return event;
}
static void passage(system_event_t scan_event)
{
    system_event_t event = {.id = controller_next_event_id(), .type = EVENT_PASSAGE_CONFIRMED,
        .data.passage = {.transaction_id = scan_event.id, .direction = scan_event.data.scan.direction}};
    post(&event);
}
static void demo_task(void *arg)
{
    (void)arg;
    ESP_LOGW("role1_demo", "SYNTHETIC INPUTS / FAKE ACTUATORS / NO SD PERSISTENCE");
    vTaskDelay(pdMS_TO_TICKS(500));
    system_event_t alice = scan(1, DIRECTION_ENTRY); passage(alice);
    post(&alice); /* Exact same event ID: duplicate must not open the door again. */
    scan(1, DIRECTION_ENTRY); /* New scan: already inside. */
    system_event_t brian = scan(2, DIRECTION_ENTRY); passage(brian);
    ESP_ERROR_CHECK(controller_demo_set_schedule(false, pdMS_TO_TICKS(1000)));
    scan(1, DIRECTION_EXIT); /* Required denial even though Alice is inside. */
    ESP_ERROR_CHECK(controller_demo_set_schedule(true, pdMS_TO_TICKS(1000)));
    alice = scan(1, DIRECTION_EXIT); passage(alice);
    passage(alice); /* A new passage event cannot reuse a completed transaction. */
    scan(99, DIRECTION_ENTRY); /* Unknown card. */
    system_event_t room = {.id = controller_next_event_id(), .type = EVENT_ROOM_STATE,
        .data.room = {.room_id = 1, .occupancy = ROOM_OCCUPIED, .light_on = true}};
    post(&room);
    room.id = controller_next_event_id(); room.data.room.room_id = 2;
    room.data.room.occupancy = ROOM_EMPTY; room.data.room.light_on = false; post(&room);
    system_event_t carol = scan(3, DIRECTION_ENTRY);
    /* Approved scan without crossing must expire and not add Carol. */
    vTaskDelay(pdMS_TO_TICKS(SYSTEM_PASSAGE_TIMEOUT_MS + 300));
    passage(carol);
    vTaskDelay(pdMS_TO_TICKS(200));
    system_snapshot_t s;
    ESP_ERROR_CHECK(controller_get_snapshot(&s, pdMS_TO_TICKS(1000)));
    bool passed = s.building.count == 1 && s.building.users[0] == 2 &&
        !s.pending[0].active && !s.pending[1].active &&
        s.rooms[0].known && s.rooms[0].occupancy == ROOM_OCCUPIED && s.rooms[0].light_on &&
        s.rooms[1].known && s.rooms[1].occupancy == ROOM_EMPTY && !s.rooms[1].light_on;
    printf("\nROLE1 DEMO %s: Brian(id=2) is inside; Alice/Carol are outside; rooms independent.\n",
           passed ? "PASS" : "FAIL");
    vTaskDelete(NULL);
}
esp_err_t role1_demo_start(void)
{
    return xTaskCreate(demo_task, "role1_demo", 4096, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
#else
esp_err_t role1_demo_start(void) { return ESP_ERR_NOT_SUPPORTED; }
#endif
