#ifndef INTEGRATION_H
#define INTEGRATION_H

#include "esp_err.h"
#include "system_types.h"

/* The four external services controller.c currently calls.
 * These are adapter entry points, not a required design for other members.
 * Their implementations can call teammates' own APIs when integrating.
 * All calls must return quickly; unfinished providers return NOT_SUPPORTED.
 */

/* Role 2: ESP_OK + known=false means an unregistered tag, not a lookup fault. */
esp_err_t rfid_lookup_user(const system_uid_t *uid, system_user_id_t *user, bool *known);

/* Role 2: accept a door command without waiting for the whole passage. */
esp_err_t door_set_open(system_direction_t direction, bool open);

/* Role 4: check both directions against valid wall-clock time/configuration. */
esp_err_t schedule_access_allowed(system_direction_t direction, bool *allowed);

/* Role 5: enqueue a COPY for the storage task; do not write SD synchronously. */
esp_err_t storage_queue_record(const system_audit_record_t *record);

#endif
