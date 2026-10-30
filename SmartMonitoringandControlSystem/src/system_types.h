#ifndef SYSTEM_TYPES_H
#define SYSTEM_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DOOR_ENTRY = 0,
    DOOR_EXIT
} door_direction_t;

typedef struct {
    char uid[9];              // Four-byte UID represented as 8 hex characters
    char participant[32];
    door_direction_t direction;
} rfid_scan_event_t;

#endif