/* Temporary adapter implementations required by controller.c.
 * Replace each body with a call to its owner's implementation when integrating.
 * Teammates can choose their own files, tasks, driver APIs, and startup functions.
 * No RFID, door, clock, or SD work is performed here.
 */
#include "integration.h"

esp_err_t rfid_lookup_user(const system_uid_t *uid, system_user_id_t *user, bool *known)
{
    if (!uid || !user || !known) return ESP_ERR_INVALID_ARG;
    *user = 0;
    *known = false;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t door_set_open(system_direction_t direction, bool open)
{
    (void)open;
    if (direction != DIRECTION_ENTRY && direction != DIRECTION_EXIT) return ESP_ERR_INVALID_ARG;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t storage_queue_record(const system_audit_record_t *record)
{
    if (!record) return ESP_ERR_INVALID_ARG;
    return ESP_ERR_NOT_SUPPORTED;
}
