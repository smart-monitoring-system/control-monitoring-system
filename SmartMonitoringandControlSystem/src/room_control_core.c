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

static room_counter_result_t complete_entry(room_counter_t *counter)
{
    if (counter->people_count >= SYSTEM_MAX_USERS) return mark_unknown(counter);
    ++counter->people_count;
    room_counter_result_t result = {
        .occupancy_changed = counter->occupancy != ROOM_OCCUPIED,
        .count_changed = true,
    };
    counter->occupancy = ROOM_OCCUPIED;
    counter->passage_state = ROOM_PASSAGE_IDLE;
    counter->passage_started_ms = 0;
    return result;
}

static room_counter_result_t complete_exit(room_counter_t *counter)
{
    if (counter->people_count == 0) return mark_unknown(counter);
    --counter->people_count;
    room_counter_result_t result = {
        .occupancy_changed = counter->people_count == 0 && counter->occupancy != ROOM_EMPTY,
        .count_changed = true,
    };
    counter->occupancy = counter->people_count == 0 ? ROOM_EMPTY : ROOM_OCCUPIED;
    counter->passage_state = ROOM_PASSAGE_IDLE;
    counter->passage_started_ms = 0;
    return result;
}

room_counter_result_t room_counter_process(room_counter_t *counter,
    bool a_rising, bool b_rising, bool a_active, bool b_active, uint64_t now_ms)
{
    room_counter_result_t result = {0};
    if (!counter || counter->occupancy == ROOM_UNKNOWN) return result;

    if (counter->passage_state != ROOM_PASSAGE_IDLE &&
        now_ms - counter->passage_started_ms >= ROOM_CROSSING_TIMEOUT_MS) {
        /* Never commit a stalled or only partly observed crossing. */
        return mark_unknown(counter);
    }

    switch (counter->passage_state) {
    case ROOM_PASSAGE_IDLE:
        if (a_active && b_active) return mark_unknown(counter);
        if (a_rising) {
            counter->passage_state = ROOM_PASSAGE_WAIT_FOR_B;
            counter->passage_started_ms = now_ms;
        } else if (b_rising) {
            counter->passage_state = ROOM_PASSAGE_WAIT_FOR_A;
            counter->passage_started_ms = now_ms;
        } else if (a_active || b_active) {
            return mark_unknown(counter);
        }
        break;
    case ROOM_PASSAGE_WAIT_FOR_B:
        if (a_rising || !a_active) return mark_unknown(counter);
        if (b_rising) counter->passage_state = ROOM_PASSAGE_ENTRY_WAIT_A_CLEAR;
        break;
    case ROOM_PASSAGE_WAIT_FOR_A:
        if (b_rising || !b_active) return mark_unknown(counter);
        if (a_rising) counter->passage_state = ROOM_PASSAGE_EXIT_WAIT_B_CLEAR;
        break;
    case ROOM_PASSAGE_ENTRY_WAIT_A_CLEAR:
        /* A must clear first. B clearing first is an entry turnaround. */
        if (a_rising || b_rising || !b_active) return mark_unknown(counter);
        if (!a_active) counter->passage_state = ROOM_PASSAGE_ENTRY_WAIT_B_CLEAR;
        break;
    case ROOM_PASSAGE_ENTRY_WAIT_B_CLEAR:
        if (a_rising || b_rising || a_active) return mark_unknown(counter);
        if (!b_active) return complete_entry(counter);
        break;
    case ROOM_PASSAGE_EXIT_WAIT_B_CLEAR:
        /* B must clear first. A clearing first is an exit turnaround. */
        if (a_rising || b_rising || !a_active) return mark_unknown(counter);
        if (!b_active) counter->passage_state = ROOM_PASSAGE_EXIT_WAIT_A_CLEAR;
        break;
    case ROOM_PASSAGE_EXIT_WAIT_A_CLEAR:
        if (a_rising || b_rising || b_active) return mark_unknown(counter);
        if (!a_active) return complete_exit(counter);
        break;
    default:
        return mark_unknown(counter);
    }
    return result;
}
