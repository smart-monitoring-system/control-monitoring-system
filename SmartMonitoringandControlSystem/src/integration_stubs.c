#include "integration.h"
#include "identity.h"
#include "door.h"

esp_err_t rfid_lookup_user(
    const system_uid_t *uid,
    system_user_id_t *user,
    bool *known
)
{
    return identity_lookup_user(uid, user, known);
}

esp_err_t door_set_open(
    system_direction_t direction,
    bool open
    
)
{
    return door_set_output(direction, open);
}

esp_err_t schedule_access_allowed(
    system_direction_t direction,
    bool *allowed
)
{
    if (!allowed ||
        (direction != DIRECTION_ENTRY &&
         direction != DIRECTION_EXIT)) {
        return ESP_ERR_INVALID_ARG;
    }

    *allowed = false;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t storage_queue_record(
    const system_audit_record_t *record
)
{
    if (!record) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_ERR_NOT_SUPPORTED;
}