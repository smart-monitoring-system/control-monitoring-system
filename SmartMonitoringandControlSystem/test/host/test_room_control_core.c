#include <assert.h>
#include <stdio.h>

#include "room_control_core.h"

typedef struct {
    room_counter_t counter;
    bool a;
    bool b;
    uint64_t now;
} fixture_t;

static fixture_t empty_room(void)
{
    fixture_t f = {0};
    room_counter_init(&f.counter);
    assert(f.counter.occupancy == ROOM_UNKNOWN);
    room_counter_reconcile_empty(&f.counter);
    assert(f.counter.occupancy == ROOM_EMPTY && f.counter.people_count == 0);
    return f;
}

/* Edges come from consecutive sensor samples, as in the GPIO task. */
static room_counter_result_t sample(fixture_t *f, bool a, bool b)
{
    f->now += 100;
    room_counter_result_t result = room_counter_process(
        &f->counter, a && !f->a, b && !f->b, a, b, f->now
    );
    f->a = a;
    f->b = b;
    return result;
}

static void enter_room(fixture_t *f)
{
    size_t before = f->counter.people_count;
    assert(!sample(f, true, false).count_changed);
    assert(!sample(f, true, true).count_changed);
    assert(f->counter.people_count == before);
    assert(!sample(f, false, true).count_changed);
    assert(f->counter.people_count == before);
    room_counter_result_t result = sample(f, false, false);
    assert(result.count_changed && f->counter.people_count == before + 1);
    assert(f->counter.occupancy == ROOM_OCCUPIED);
    assert(f->counter.passage_state == ROOM_PASSAGE_IDLE);
}

static void exit_room(fixture_t *f)
{
    size_t before = f->counter.people_count;
    assert(before > 0);
    assert(!sample(f, false, true).count_changed);
    assert(!sample(f, true, true).count_changed);
    assert(f->counter.people_count == before);
    assert(!sample(f, true, false).count_changed);
    /* In particular, the last person's light cannot switch off yet. */
    assert(f->counter.occupancy == ROOM_OCCUPIED);
    room_counter_result_t result = sample(f, false, false);
    assert(result.count_changed && f->counter.people_count == before - 1);
    assert(f->counter.occupancy == (before == 1 ? ROOM_EMPTY : ROOM_OCCUPIED));
    assert(f->counter.passage_state == ROOM_PASSAGE_IDLE);
}

static void test_completed_crossings(void)
{
    fixture_t f = empty_room();
    enter_room(&f);
    enter_room(&f);
    exit_room(&f);
    exit_room(&f);
    assert(f.counter.people_count == 0 && f.counter.occupancy == ROOM_EMPTY);
}

static void test_exit_turnaround_does_not_empty_room(void)
{
    fixture_t f = empty_room();
    enter_room(&f);
    sample(&f, false, true);
    sample(&f, true, true);
    /* Turn back inside: A clears before B, reversing the exit release order. */
    room_counter_result_t result = sample(&f, false, true);
    assert(result.ambiguous && !result.count_changed);
    assert(f.counter.people_count == 1 && f.counter.occupancy == ROOM_UNKNOWN);
    sample(&f, false, false);
    assert(f.counter.people_count == 1 && f.counter.occupancy == ROOM_UNKNOWN);
}

static void test_entry_turnaround_does_not_add_person(void)
{
    fixture_t f = empty_room();
    sample(&f, true, false);
    sample(&f, true, true);
    room_counter_result_t result = sample(&f, true, false);
    assert(result.ambiguous && !result.count_changed);
    assert(f.counter.people_count == 0 && f.counter.occupancy == ROOM_UNKNOWN);
}

static void test_partial_and_unobserved_crossings_are_unknown(void)
{
    fixture_t f = empty_room();
    sample(&f, true, false);
    assert(sample(&f, false, false).ambiguous);
    assert(f.counter.people_count == 0 && f.counter.occupancy == ROOM_UNKNOWN);

    f = empty_room();
    enter_room(&f);
    sample(&f, false, true);
    assert(sample(&f, false, false).ambiguous);
    assert(f.counter.people_count == 1 && f.counter.occupancy == ROOM_UNKNOWN);

    f = empty_room();
    assert(sample(&f, true, true).ambiguous);

    f = empty_room();
    sample(&f, true, false);
    sample(&f, true, true);
    /* Clearing both between samples hides the release order. */
    assert(sample(&f, false, false).ambiguous);
    assert(f.counter.people_count == 0);
}

static void test_timeout_in_each_crossing_phase(void)
{
    const bool paths[][3][2] = {
        {{true, false}, {true, true}, {false, true}},
        {{false, true}, {true, true}, {true, false}},
    };
    for (size_t direction = 0; direction < 2; ++direction) {
        for (size_t phase = 0; phase < 3; ++phase) {
            fixture_t f = empty_room();
            enter_room(&f);
            uint64_t start = f.now + 100;
            for (size_t i = 0; i <= phase; ++i) {
                sample(&f, paths[direction][i][0], paths[direction][i][1]);
            }
            uint64_t deadline = start + ROOM_CROSSING_TIMEOUT_MS;
            room_counter_result_t result = room_counter_process(
                &f.counter, false, false, f.a, f.b, deadline - 1
            );
            assert(!result.ambiguous && f.counter.people_count == 1);
            result = room_counter_process(&f.counter, false, false, f.a, f.b, deadline);
            assert(result.ambiguous && !result.count_changed);
            assert(f.counter.people_count == 1 && f.counter.occupancy == ROOM_UNKNOWN);
        }
    }
}

static void test_retrigger_and_capacity(void)
{
    fixture_t f = empty_room();
    sample(&f, true, false);
    sample(&f, true, true);
    sample(&f, false, true);
    assert(sample(&f, true, true).ambiguous);
    assert(f.counter.people_count == 0);

    f = empty_room();
    sample(&f, false, true);
    sample(&f, true, true);
    sample(&f, true, false);
    assert(sample(&f, false, false).ambiguous);
    assert(f.counter.people_count == 0); /* No unsigned underflow. */

    f = empty_room();
    f.counter.people_count = SYSTEM_MAX_USERS;
    f.counter.occupancy = ROOM_OCCUPIED;
    sample(&f, true, false);
    sample(&f, true, true);
    sample(&f, false, true);
    assert(sample(&f, false, false).ambiguous);
    assert(f.counter.people_count == SYSTEM_MAX_USERS);
}

static void test_unknown_requires_reconciliation(void)
{
    fixture_t f = {0};
    room_counter_init(&f.counter);
    sample(&f, true, false);
    sample(&f, true, true);
    sample(&f, false, true);
    sample(&f, false, false);
    assert(f.counter.occupancy == ROOM_UNKNOWN && f.counter.people_count == 0);
    room_counter_reconcile_empty(&f.counter);
    enter_room(&f);
    sample(&f, false, true);
    sample(&f, true, true);
    assert(sample(&f, false, true).ambiguous);
    sample(&f, false, false);
    sample(&f, true, false);
    sample(&f, true, true);
    sample(&f, false, true);
    sample(&f, false, false);
    assert(f.counter.occupancy == ROOM_UNKNOWN && f.counter.people_count == 1);
    room_counter_reconcile_empty(&f.counter);
    assert(f.counter.occupancy == ROOM_EMPTY && f.counter.people_count == 0);
    enter_room(&f);
}

static void test_rooms_are_independent(void)
{
    fixture_t first = empty_room(), second = empty_room();
    enter_room(&first);
    assert(second.counter.people_count == 0 && second.counter.occupancy == ROOM_EMPTY);
    sample(&first, false, true);
    sample(&first, true, true);
    assert(sample(&first, false, true).ambiguous);
    enter_room(&second);
    assert(first.counter.occupancy == ROOM_UNKNOWN && first.counter.people_count == 1);
    assert(second.counter.occupancy == ROOM_OCCUPIED && second.counter.people_count == 1);
}

int main(void)
{
    test_completed_crossings();
    test_exit_turnaround_does_not_empty_room();
    test_entry_turnaround_does_not_add_person();
    test_partial_and_unobserved_crossings_are_unknown();
    test_timeout_in_each_crossing_phase();
    test_retrigger_and_capacity();
    test_unknown_requires_reconciliation();
    test_rooms_are_independent();
    puts("PASS: eight room suites (completed crossings, turnarounds, ambiguity, timeouts, capacity, recovery, independence)");
    return 0;
}
