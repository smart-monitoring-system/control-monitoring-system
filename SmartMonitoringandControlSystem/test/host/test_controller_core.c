#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "controller_core.h"
#include "occupancy.h"
static unsigned id;
static system_event_t scan_event(system_direction_t direction)
{
    return (system_event_t){.id = ++id, .type = EVENT_ACCESS_SCAN,
        .data.scan = {.direction = direction, .uid = {4, {1,2,3,4}}}};
}
static access_facts_t allowed(system_user_id_t user)
{
    return (access_facts_t){.identity_ready = true, .identity_known = true,
        .user_id = user, .schedule_ready = true, .schedule_open = true};
}
static controller_effect_t scan(controller_core_t *c, system_user_id_t user, system_direction_t d,
    bool open, uint64_t now, system_event_t *saved)
{
    system_event_t e = scan_event(d);
    access_facts_t f = allowed(user); f.schedule_open = open;
    if (saved) *saved = e;
    return controller_core_process(c, &e, &f, now);
}
static controller_effect_t confirm(controller_core_t *c, system_event_t original, uint64_t now)
{
    system_event_t p = {.id = ++id, .type = EVENT_PASSAGE_CONFIRMED,
        .data.passage = {.transaction_id = original.id, .direction = original.data.scan.direction}};
    return controller_core_process(c, &p, NULL, now);
}
static void test_permissions(void)
{
    controller_core_t c; controller_core_init(&c);
    assert(c.snapshot.building.count == 0 && !c.snapshot.rooms[0].known);
    system_event_t e = scan_event(DIRECTION_ENTRY);
    assert(controller_core_process(&c, &e, NULL, 0).result == RESULT_NOT_READY);
    access_facts_t f = allowed(1); f.identity_known = false; e.id = ++id;
    assert(controller_core_process(&c, &e, &f, 0).result == RESULT_UNKNOWN_USER);
    f = allowed(1); f.schedule_ready = false; e.id = ++id;
    assert(controller_core_process(&c, &e, &f, 0).result == RESULT_NOT_READY);
    assert(scan(&c, 1, DIRECTION_ENTRY, false, 0, NULL).result == RESULT_SCHEDULE_CLOSED);
    assert(occupancy_enter(&c.snapshot.building, 1) == RESULT_ENTERED);
    assert(scan(&c, 1, DIRECTION_EXIT, false, 0, NULL).result == RESULT_SCHEDULE_CLOSED);
    assert(c.snapshot.building.count == 1 && !c.snapshot.pending[1].active);
    assert(scan(&c, 0, DIRECTION_ENTRY, true, 0, NULL).result == RESULT_INVALID_EVENT);
}
static void test_transactions_and_duplicates(void)
{
    controller_core_t c; controller_core_init(&c);
    system_event_t e;
    controller_effect_t out = scan(&c, 1, DIRECTION_ENTRY, true, 100, &e);
    assert(out.result == RESULT_ACCESS_PENDING && out.open_mask == 1);
    assert(c.snapshot.building.count == 0); /* Scan approval is not passage. */
    access_facts_t f = allowed(1);
    assert(controller_core_process(&c, &e, &f, 200).result == RESULT_DUPLICATE_EVENT);
    assert(c.snapshot.pending[0].deadline_ms == 100 + SYSTEM_PASSAGE_TIMEOUT_MS);
    system_event_t wrong = e; wrong.id += 100000;
    assert(confirm(&c, wrong, 300).result == RESULT_UNEXPECTED_PASSAGE);
    assert(c.snapshot.pending[0].active && c.snapshot.building.count == 0);
    assert(confirm(&c, e, 400).result == RESULT_ENTERED);
    assert(c.snapshot.building.count == 1 && occupancy_contains(&c.snapshot.building, 1));
    assert(confirm(&c, e, 450).result == RESULT_UNEXPECTED_PASSAGE);
    assert(scan(&c, 1, DIRECTION_ENTRY, true, 500, NULL).result == RESULT_ALREADY_INSIDE);
    assert(scan(&c, 2, DIRECTION_EXIT, true, 500, NULL).result == RESULT_NOT_INSIDE);
    scan(&c, 1, DIRECTION_EXIT, true, 600, &e);
    system_event_t p = {.id=++id, .type=EVENT_PASSAGE_CONFIRMED,
        .data.passage={.transaction_id=e.id,.direction=DIRECTION_EXIT}};
    assert(controller_core_process(&c, &p, NULL, 700).result == RESULT_EXITED);
    assert(controller_core_process(&c, &p, NULL, 800).result == RESULT_DUPLICATE_EVENT);
    assert(c.snapshot.building.count == 0);
}
static void test_interleaving(void)
{
    controller_core_t c; controller_core_init(&c);
    occupancy_enter(&c.snapshot.building, 1);
    system_event_t arrival, departure;
    assert(scan(&c, 2, DIRECTION_ENTRY, true, 100, &arrival).result == RESULT_ACCESS_PENDING);
    assert(scan(&c, 3, DIRECTION_ENTRY, true, 100, NULL).result == RESULT_BUSY);
    assert(scan(&c, 1, DIRECTION_EXIT, true, 100, &departure).result == RESULT_ACCESS_PENDING);
    assert(confirm(&c, arrival, 200).result == RESULT_ENTERED);
    assert(confirm(&c, departure, 200).result == RESULT_EXITED);
    assert(c.snapshot.building.count == 1 && occupancy_contains(&c.snapshot.building, 2));
}
static void test_timeout_and_cancellation(void)
{
    controller_core_t c; controller_core_init(&c);
    system_event_t e;
    scan(&c, 1, DIRECTION_ENTRY, true, 100, &e);
    assert(controller_core_tick(&c, 100 + SYSTEM_PASSAGE_TIMEOUT_MS - 1).result == RESULT_IDLE);
    controller_effect_t out = controller_core_tick(&c, 100 + SYSTEM_PASSAGE_TIMEOUT_MS);
    assert(out.expired_mask == 1 && out.close_mask == 1);
    assert(c.snapshot.building.count == 0 && !c.snapshot.pending[0].active);
    assert(confirm(&c, e, 100 + SYSTEM_PASSAGE_TIMEOUT_MS + 1).result == RESULT_UNEXPECTED_PASSAGE);
    scan(&c, 1, DIRECTION_ENTRY, true, 10000, &e);
    assert(confirm(&c, e, 10000 + SYSTEM_PASSAGE_TIMEOUT_MS).result == RESULT_PASSAGE_EXPIRED);
    scan(&c, 1, DIRECTION_ENTRY, true, 20000, &e);
    controller_core_cancel(&c, DIRECTION_ENTRY); /* Door failure cancels approval. */
    assert(confirm(&c, e, 20001).result == RESULT_UNEXPECTED_PASSAGE);
    assert(c.snapshot.building.count == 0);
    scan(&c, 1, DIRECTION_ENTRY, true, 30000, &e);
    system_event_t revoke = {.id = ++id, .type = EVENT_REVOKE_PENDING};
    out = controller_core_process(&c, &revoke, NULL, 30001);
    assert(out.result == RESULT_ACCESS_REVOKED && out.close_mask == 1);
    assert(confirm(&c, e, 30002).result == RESULT_UNEXPECTED_PASSAGE);
    assert(c.snapshot.building.count == 0);
}
static void test_capacity_and_invalid_input(void)
{
    controller_core_t c; controller_core_init(&c);
    for (unsigned i = 1; i <= SYSTEM_MAX_USERS; ++i) assert(occupancy_enter(&c.snapshot.building, i) == RESULT_ENTERED);
    assert(scan(&c, SYSTEM_MAX_USERS+1, DIRECTION_ENTRY, true, 0, NULL).result == RESULT_CAPACITY);
    assert(c.snapshot.building.count == SYSTEM_MAX_USERS);
    assert(occupancy_exit(&c.snapshot.building, 1) == RESULT_EXITED);
    assert(occupancy_exit(&c.snapshot.building, 1) == RESULT_NOT_INSIDE);
    system_event_t e;
    assert(scan(&c, SYSTEM_MAX_USERS+1, DIRECTION_ENTRY, true, 0, &e).result == RESULT_ACCESS_PENDING);
    assert(confirm(&c, e, 1).result == RESULT_ENTERED);
    system_event_t invalid = {.id = ++id, .type = (system_event_type_t)999};
    assert(controller_core_process(&c, &invalid, NULL, 0).result == RESULT_INVALID_EVENT);
    invalid.id = 0;
    assert(controller_core_process(&c, &invalid, NULL, 0).result == RESULT_INVALID_EVENT);
    assert(c.snapshot.building.count == SYSTEM_MAX_USERS);
}
static void test_room_independence(void)
{
    controller_core_t c; controller_core_init(&c);
    system_event_t e = {.id=++id,.type=EVENT_ROOM_STATE,
        .data.room={.room_id=1,.occupancy=ROOM_OCCUPIED,.light_on=true}};
    assert(controller_core_process(&c, &e, NULL, 0).result == RESULT_ROOM_UPDATED);
    assert(c.snapshot.rooms[0].known && c.snapshot.rooms[0].light_on);
    assert(!c.snapshot.rooms[1].known && c.snapshot.building.count == 0);
    e.id=++id; e.data.room.room_id=3;
    assert(controller_core_process(&c, &e, NULL, 0).result == RESULT_INVALID_EVENT);
    e.id=++id; e.data.room.room_id=2; e.data.room.occupancy=ROOM_EMPTY; e.data.room.light_on=false;
    assert(controller_core_process(&c, &e, NULL, 0).result == RESULT_ROOM_UPDATED);
    assert(c.snapshot.rooms[0].light_on && !c.snapshot.rooms[1].light_on);
}
int main(void)
{
    test_permissions(); test_transactions_and_duplicates(); test_interleaving();
    test_timeout_and_cancellation(); test_capacity_and_invalid_input(); test_room_independence();
    puts("PASS: six controller suites (permissions, transactions, interleaving, timeouts, capacity, rooms)");
    return 0;
}
