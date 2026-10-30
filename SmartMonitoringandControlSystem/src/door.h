#ifndef DOOR_H
#define DOOR_H

#include "system_types.h"

void door_init(void);

/* Called by the controller only after it authorises access. */
void open_door(door_direction_t direction);

/* Call repeatedly to close doors after the unlock period. */
void door_update(void);

#endif