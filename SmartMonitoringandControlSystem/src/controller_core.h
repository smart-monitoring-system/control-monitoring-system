#ifndef CONTROLLER_CORE_H
#define CONTROLLER_CORE_H
#include "access_control.h"
typedef struct {
    system_snapshot_t snapshot;
    system_event_id_t recent[SYSTEM_RECENT_EVENT_COUNT];
    size_t next_recent;
} controller_core_t;
typedef struct {
    system_result_t result;
    system_user_id_t user_id;
    uint8_t open_mask;
    uint8_t close_mask;
    uint8_t expired_mask;
} controller_effect_t;
/* Pure C model: no ESP-IDF, hardware, files, or synchronization here. */
void controller_core_init(controller_core_t *core);
controller_effect_t controller_core_process(controller_core_t *core,
    const system_event_t *event, const access_facts_t *facts, uint64_t now_ms);
controller_effect_t controller_core_tick(controller_core_t *core, uint64_t now_ms);
void controller_core_cancel(controller_core_t *core, system_direction_t direction);
#endif
