#ifndef RFID_H
#define RFID_H

#include <stdbool.h>
#include "system_types.h"

void rfid_init(void);

/* Returns true when a new card scan is detected. */
bool rfid_poll(rfid_scan_event_t *event);

/* Returns the registered person's name, or NULL if unknown. */
const char *lookup_user(const char *uid);

#endif