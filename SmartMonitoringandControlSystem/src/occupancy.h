#ifndef OCCUPANCY_H
#define OCCUPANCY_H
#include "system_types.h"
/* Called by the controller only. These functions do not provide locking. */
bool occupancy_contains(const occupancy_state_t *state, system_user_id_t user);
system_result_t occupancy_enter(occupancy_state_t *state, system_user_id_t user);
system_result_t occupancy_exit(occupancy_state_t *state, system_user_id_t user);
#endif
