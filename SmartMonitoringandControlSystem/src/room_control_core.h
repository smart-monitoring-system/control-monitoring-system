#ifndef ROOM_CONTROL_CORE_H
#define ROOM_CONTROL_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "system_types.h"

/* Ordered, overlapping beams: A -> A+B -> B -> clear enters; reverse exits. */
#ifndef ROOM_CROSSING_TIMEOUT_MS
#define ROOM_CROSSING_TIMEOUT_MS 2000U
#endif

typedef enum {
    ROOM_PASSAGE_IDLE = 0,
    ROOM_PASSAGE_WAIT_FOR_B,
    ROOM_PASSAGE_WAIT_FOR_A,
    ROOM_PASSAGE_ENTRY_WAIT_A_CLEAR,
    ROOM_PASSAGE_ENTRY_WAIT_B_CLEAR,
    ROOM_PASSAGE_EXIT_WAIT_B_CLEAR,
    ROOM_PASSAGE_EXIT_WAIT_A_CLEAR,
} room_passage_state_t;

typedef struct {
    room_occupancy_t occupancy;
    size_t people_count;
    room_passage_state_t passage_state;
    uint64_t passage_started_ms;
} room_counter_t;

typedef struct {
    bool occupancy_changed;
    bool count_changed;
    bool ambiguous;
} room_counter_result_t;

void room_counter_init(room_counter_t *counter);
room_counter_result_t room_counter_reconcile_empty(room_counter_t *counter);
room_counter_result_t room_counter_process(room_counter_t *counter,
    bool a_rising, bool b_rising, bool a_active, bool b_active, uint64_t now_ms);

#endif
