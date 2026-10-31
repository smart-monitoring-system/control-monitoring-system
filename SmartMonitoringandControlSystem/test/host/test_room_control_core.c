#include <assert.h>
#include <stdio.h>

#include "room_control_core.h"

static void reconcile_empty(room_counter_t *counter)
{
    room_counter_result_t result = room_counter_reconcile_empty(counter);
    assert(counter->occupancy == ROOM_EMPTY);
    assert(counter->people_count == 0);
    assert(result.occupancy_changed || !result.count_changed);
}

static void clear_doorway(room_counter_t *counter, uint64_t now)
{
    (void)room_counter_process(counter, false, false, false, false, now);
    assert(counter->passage_state == ROOM_PASSAGE_IDLE);
}

static void enter_room(room_counter_t *counter, uint64_t now)
{
    room_counter_result_t result = room_counter_process(counter, true, false, true, false, now);
    assert(!result.count_changed && counter->passage_state == ROOM_PASSAGE_WAIT_FOR_B);
    result = room_counter_process(counter, false, true, true, true, now + 100);
    assert(result.count_changed && counter->occupancy == ROOM_OCCUPIED);
    clear_doorway(counter, now + 200);
}

static void exit_room(room_counter_t *counter, uint64_t now)
{
    room_counter_result_t result = room_counter_process(counter, false, true, false, true, now);
    assert(!result.count_changed && counter->passage_state == ROOM_PASSAGE_WAIT_FOR_A);
    result = room_counter_process(counter, true, false, true, true, now + 100);
    assert(result.count_changed);
    clear_doorway(counter, now + 200);
}

static void test_clean_directional_crossings(void)
{
    room_counter_t counter;
    room_counter_init(&counter);
    assert(counter.occupancy == ROOM_UNKNOWN);
    reconcile_empty(&counter);
    enter_room(&counter, 100);
    assert(counter.people_count == 1 && counter.occupancy == ROOM_OCCUPIED);
    enter_room(&counter, 400);
    assert(counter.people_count == 2 && counter.occupancy == ROOM_OCCUPIED);
    exit_room(&counter, 700);
    assert(counter.people_count == 1 && counter.occupancy == ROOM_OCCUPIED);
    exit_room(&counter, 1000);
    assert(counter.people_count == 0 && counter.occupancy == ROOM_EMPTY);
}

static void test_ambiguous_sequences_stay_safe(void)
{
    room_counter_t counter;
    room_counter_init(&counter);
    reconcile_empty(&counter);
    room_counter_result_t result = room_counter_process(&counter, true, true, true, true, 0);
    assert(result.ambiguous && counter.occupancy == ROOM_UNKNOWN);

    reconcile_empty(&counter);
    room_counter_process(&counter, true, false, true, false, 100);
    result = room_counter_process(&counter, false, false, false, false,
                                  100 + ROOM_CROSSING_TIMEOUT_MS);
    assert(result.ambiguous && counter.occupancy == ROOM_UNKNOWN);

    reconcile_empty(&counter);
    result = room_counter_process(&counter, false, true, false, true, 5000);
    assert(!result.ambiguous);
    result = room_counter_process(&counter, true, false, true, true, 5100);
    assert(result.ambiguous && counter.occupancy == ROOM_UNKNOWN);
}

static void test_close_following_crossing_is_not_guessed(void)
{
    room_counter_t counter;
    room_counter_init(&counter);
    reconcile_empty(&counter);
    enter_room(&counter, 100);
    assert(counter.people_count == 1);

    room_counter_process(&counter, true, false, true, false, 500);
    room_counter_result_t result = room_counter_process(&counter, false, true, true, true, 600);
    assert(result.count_changed && counter.passage_state == ROOM_PASSAGE_WAIT_CLEAR);
    result = room_counter_process(&counter, true, false, true, true, 650);
    assert(result.ambiguous && counter.occupancy == ROOM_UNKNOWN);
}

int main(void)
{
    test_clean_directional_crossings();
    test_ambiguous_sequences_stay_safe();
    test_close_following_crossing_is_not_guessed();
    puts("PASS: room directional counts, ambiguity handling, restart recovery");
    return 0;
}
