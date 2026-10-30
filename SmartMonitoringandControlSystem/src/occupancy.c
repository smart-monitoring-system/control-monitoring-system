#include "occupancy.h"
bool occupancy_contains(const occupancy_state_t *s, system_user_id_t user)
{
    if (!s || !user) return false;
    for (size_t i = 0; i < s->count; ++i) if (s->users[i] == user) return true;
    return false;
}
system_result_t occupancy_enter(occupancy_state_t *s, system_user_id_t user)
{
    if (!s || !user) return RESULT_INVALID_EVENT;
    if (occupancy_contains(s, user)) return RESULT_ALREADY_INSIDE;
    if (s->count == SYSTEM_MAX_USERS) return RESULT_CAPACITY;
    s->users[s->count++] = user;
    return RESULT_ENTERED;
}
system_result_t occupancy_exit(occupancy_state_t *s, system_user_id_t user)
{
    if (!s || !user) return RESULT_INVALID_EVENT;
    for (size_t i = 0; i < s->count; ++i) {
        if (s->users[i] == user) {
            s->users[i] = s->users[--s->count];
            s->users[s->count] = 0;
            return RESULT_EXITED;
        }
    }
    return RESULT_NOT_INSIDE;
}
