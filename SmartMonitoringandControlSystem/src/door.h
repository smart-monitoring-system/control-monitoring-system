#ifndef DOOR_H
#define DOOR_H

#include <stdbool.h>
#include "esp_err.h"
#include "system_types.h"

esp_err_t door_init(void);

esp_err_t door_set_output(system_direction_t direction, bool open);

#endif