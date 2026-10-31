#ifndef IDENTITY_H
#define IDENTITY_H

#include "system_types.h"
#include "esp_err.h"

/* Find the user associated with an RFID card UID. */
esp_err_t identity_lookup_user(
    const system_uid_t *uid,
    system_user_id_t *user,
    bool *known
);

#endif