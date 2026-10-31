#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include "esp_err.h"

/* Has anyone set up an admin account yet? */
bool auth_any_admin_exists(void);

/* Create an admin. Allowed only if no admin exists yet, or if an admin is logged in.
 * Returns ESP_OK, or ESP_ERR_INVALID_STATE if not allowed, ESP_ERR_INVALID_ARG if
 * the name or password is unsuitable, ESP_ERR_NO_MEM if both slots are used. */
esp_err_t auth_create_admin(const char *username, const char *password);

/* Try to log in.
 * ESP_OK = logged in, ESP_FAIL = wrong name or password,
 * ESP_ERR_INVALID_STATE = temporarily locked, ESP_ERR_NOT_FOUND = no admin set up yet. */
esp_err_t auth_login(const char *username, const char *password);

void auth_logout(void);

/* Is an admin logged in? Logs out automatically after 5 minutes of no activity. */
bool auth_is_logged_in(void);

/* Name of the logged-in admin, or NULL if nobody is logged in. */
const char *auth_current_user(void);

/* Call whenever the admin does something, to restart the 5 minute countdown. */
void auth_note_activity(void);

#endif