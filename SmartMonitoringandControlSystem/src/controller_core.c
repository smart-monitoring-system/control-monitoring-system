#include <string.h>
#include "controller_core.h"
#include "occupancy.h"
static bool valid_direction(system_direction_t d) { return d == DIRECTION_ENTRY || d == DIRECTION_EXIT; }
void controller_core_init(controller_core_t *c) { memset(c, 0, sizeof(*c)); }
void controller_core_cancel(controller_core_t *c, system_direction_t d)
{
    if (valid_direction(d)) memset(&c->snapshot.pending[d], 0, sizeof(pending_passage_t));
}
controller_effect_t controller_core_tick(controller_core_t *c, uint64_t now)
{
    controller_effect_t out = {.result = RESULT_IDLE};
    for (unsigned d = 0; d < 2; ++d) {
        pending_passage_t *p = &c->snapshot.pending[d];
        if (p->active && now >= p->deadline_ms) {
            out.close_mask |= 1U << d;
            out.expired_mask |= 1U << d;
            controller_core_cancel(c, (system_direction_t)d);
        }
    }
    if (out.expired_mask) out.result = RESULT_PASSAGE_EXPIRED;
    return out;
}
controller_effect_t controller_core_process(controller_core_t *c, const system_event_t *e,
    const access_facts_t *facts, uint64_t now)
{
    /* Timeouts are enforced even when no producer sends events. */
    controller_effect_t out = controller_core_tick(c, now);
    out.result = RESULT_INVALID_EVENT;
    if (!e || !e->id) return out;
    for (size_t i = 0; i < SYSTEM_RECENT_EVENT_COUNT; ++i) {
        if (c->recent[i] == e->id) { out.result = RESULT_DUPLICATE_EVENT; return out; }
    }
    c->recent[c->next_recent] = e->id;
    c->next_recent = (c->next_recent + 1) % SYSTEM_RECENT_EVENT_COUNT;
    if (e->type == EVENT_ACCESS_SCAN) {
        system_direction_t d = e->data.scan.direction;
        if (!valid_direction(d) || (e->data.scan.uid.length != 4 &&
            e->data.scan.uid.length != 7 && e->data.scan.uid.length != 10)) return out;
        if (facts && facts->identity_ready && facts->identity_known) out.user_id = facts->user_id;
        out.result = access_control_check(facts);
        if (out.result != RESULT_ACCESS_PENDING) return out;
        bool inside = occupancy_contains(&c->snapshot.building, facts->user_id);
        if (d == DIRECTION_ENTRY && inside) { out.result = RESULT_ALREADY_INSIDE; return out; }
        if (d == DIRECTION_EXIT && !inside) { out.result = RESULT_NOT_INSIDE; return out; }
        for (unsigned i = 0; i < 2; ++i) {
            pending_passage_t *p = &c->snapshot.pending[i];
            if (p->active && (i == (unsigned)d || p->user_id == facts->user_id)) {
                out.result = RESULT_BUSY; return out;
            }
        }
        /* Reserve the available entry slot until passage or timeout. */
        if (d == DIRECTION_ENTRY && c->snapshot.building.count >= SYSTEM_MAX_USERS) {
            out.result = RESULT_CAPACITY; return out;
        }
        c->snapshot.pending[d] = (pending_passage_t){
            .active = true, .user_id = facts->user_id, .transaction_id = e->id,
            .deadline_ms = now + SYSTEM_PASSAGE_TIMEOUT_MS
        };
        out.open_mask = 1U << d;
    } else if (e->type == EVENT_PASSAGE_CONFIRMED) {
        system_direction_t d = e->data.passage.direction;
        if (!valid_direction(d)) return out;
        pending_passage_t *p = &c->snapshot.pending[d];
        if (!p->active || !e->data.passage.transaction_id ||
            p->transaction_id != e->data.passage.transaction_id) {
            out.result = (out.expired_mask & (1U << d)) ? RESULT_PASSAGE_EXPIRED : RESULT_UNEXPECTED_PASSAGE;
            return out;
        }
        out.user_id = p->user_id;
        out.result = d == DIRECTION_ENTRY
            ? occupancy_enter(&c->snapshot.building, p->user_id)
            : occupancy_exit(&c->snapshot.building, p->user_id);
        controller_core_cancel(c, d);
        out.close_mask |= 1U << d;
    } else if (e->type == EVENT_REVOKE_PENDING) {
        for (unsigned d = 0; d < 2; ++d) {
            if (c->snapshot.pending[d].active) out.close_mask |= 1U << d;
            controller_core_cancel(c, (system_direction_t)d);
        }
        out.result = RESULT_ACCESS_REVOKED;
    } else if (e->type == EVENT_ROOM_STATE) {
        unsigned room = e->data.room.room_id;
        if (room < 1 || room > SYSTEM_ROOM_COUNT ||
            (e->data.room.occupancy != ROOM_UNKNOWN &&
             e->data.room.occupancy != ROOM_EMPTY &&
             e->data.room.occupancy != ROOM_OCCUPIED)) return out;
        c->snapshot.rooms[room-1] = (room_status_t){
            .known = e->data.room.occupancy != ROOM_UNKNOWN,
            .occupancy = e->data.room.occupancy, .light_on = e->data.room.light_on
        };
        out.result = RESULT_ROOM_UPDATED;
    }
    return out;
}
