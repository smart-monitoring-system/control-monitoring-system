#include <string.h>

#include "room_control_core.h"

static room_counter_result_t mark_unknown(room_counter_t *counter)
{
    room_counter_result_t result = {
        .occupancy_changed = counter->occupancy != ROOM_UNKNOWN,
        .ambiguous = true,
    };
    counter->occupancy = ROOM_UNKNOWN;
    counter->passage_state = ROOM_PASSAGE_IDLE;
    counter->passage_started_ms = 0;
    return result;
}

void room_counter_init(room_counter_t *counter)
{
    memset(counter, 0, sizeof(*counter));
    counter->occupancy = ROOM_UNKNOWN;
}

room_counter_result_t room_counter_reconcile_empty(room_counter_t *counter)
{
    room_counter_result_t result = {
        .occupancy_changed = counter->occupancy != ROOM_EMPTY,
        .count_changed = counter->people_count != 0,
    };
    counter->occupancy = ROOM_EMPTY;
    counter->people_count = 0;
    counter->passage_state = ROOM_PASSAGE_IDLE;
    counter->passage_started_ms = 0;
    return result;
}

static room_counter_result_t complete_entry(room_counter_t *counter, uint64_t now_ms)
{
    if (counter->people_count >= SYSTEM_MAX_USERS) return mark_unknown(counter);
    ++counter->people_count;
    room_counter_result_t result = {
        .occupancy_changed = counter->occupancy != ROOM_OCCUPIED,
        .count_changed = true,
    };
    counter->occupancy = ROOM_OCCUPIED;
    counter->passage_state = ROOM_PASSAGE_WAIT_CLEAR;
    counter->passage_started_ms = now_ms;
    return result;
}

static room_counter_result_t complete_exit(room_counter_t *counter, uint64_t now_ms)
{
    if (counter->people_count == 0) return mark_unknown(counter);
    --counter->people_count;
    room_counter_result_t result = {
        .occupancy_changed = counter->people_count == 0 && counter->occupancy != ROOM_EMPTY,
        .count_changed = true,
    };
    counter->occupancy = counter->people_count == 0 ? ROOM_EMPTY : ROOM_OCCUPIED;
    counter->passage_state = ROOM_PASSAGE_WAIT_CLEAR;
    counter->passage_started_ms = now_ms;
    return result;
}

room_counter_result_t room_counter_process(room_counter_t *counter,
    bool a_rising, bool b_rising, bool a_active, bool b_active, uint64_t now_ms)
{
    room_counter_result_t result = {0};
    if (!counter || counter->occupancy == ROOM_UNKNOWN) return result;

    if (counter->passage_state != ROOM_PASSAGE_IDLE &&
        now_ms - counter->passage_started_ms >= ROOM_CROSSING_TIMEOUT_MS) {
        /* A turn-back and a person stopped between beams are indistinguishable. */
        return mark_unknown(counter);
    }

    switch (counter->passage_state) {
    case ROOM_PASSAGE_IDLE:
        if (a_rising && b_rising) return mark_unknown(counter);
        if (a_rising) {
            counter->passage_state = ROOM_PASSAGE_WAIT_FOR_B;
            counter->passage_started_ms = now_ms;
        } else if (b_rising) {
            counter->passage_state = ROOM_PASSAGE_WAIT_FOR_A;
            counter->passage_started_ms = now_ms;
        }
        break;
    case ROOM_PASSAGE_WAIT_FOR_B:
        if (a_rising) return mark_unknown(counter);
        if (b_rising) return complete_entry(counter, now_ms);
        break;
    case ROOM_PASSAGE_WAIT_FOR_A:
        if (b_rising) return mark_unknown(counter);
        if (a_rising) return complete_exit(counter, now_ms);
        break;
    case ROOM_PASSAGE_WAIT_CLEAR:
        if (!a_active && !b_active) {
            counter->passage_state = ROOM_PASSAGE_IDLE;
            counter->passage_started_ms = 0;
        } else if (a_rising || b_rising) {
            return mark_unknown(counter);
        }
        break;
    default:
        return mark_unknown(counter);
    }
    return result;
}
