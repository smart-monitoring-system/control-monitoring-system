#include <stddef.h>
#include <string.h>
#include "auth.h"
#include "esp_timer.h"

#define ADMIN_COUNT 2
#define NAME_MAX_LEN 16
#define PASSWORD_MAX_LEN 32
#define MIN_PASSWORD_LEN 8

#define MAX_FAILED_ATTEMPTS 3
#define LOCKOUT_US (30LL * 1000000LL)               /* 30 seconds */
#define SESSION_TIMEOUT_US (5LL * 60LL * 1000000LL) /* 5 minutes */

/* One admin account: a name and a password */
typedef struct {
    bool used;
    char username[NAME_MAX_LEN];
    char password[PASSWORD_MAX_LEN];
} admin_record_t;

/* Starts empty (all zeros), so at first nobody has an account */
static admin_record_t admins[ADMIN_COUNT];

/* Who is logged in right now */
static bool logged_in = false;
static int current_admin = -1;
static int64_t last_activity_us = 0;

/* Lockout after wrong passwords */
static int failed_attempts = 0;
static int64_t locked_until_us = 0;

bool auth_any_admin_exists(void)
{
    return admins[0].used || admins[1].used;
}

esp_err_t auth_create_admin(const char *username, const char *password)
{
    if (username == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Allowed for the very first admin, otherwise an admin must be logged in */
    if (auth_any_admin_exists() && !auth_is_logged_in()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Check the name and password are sensible */
    size_t name_length = strlen(username);
    size_t password_length = strlen(password);
    if (name_length == 0 || name_length >= NAME_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password_length < MIN_PASSWORD_LEN || password_length >= PASSWORD_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Find a free slot, and refuse a name that already exists */
    int free_slot = -1;
    for (int i = 0; i < ADMIN_COUNT; i++) {
        if (!admins[i].used) {
            if (free_slot < 0) {
                free_slot = i;
            }
        } else if (strcmp(admins[i].username, username) == 0) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (free_slot < 0) {
        return ESP_ERR_NO_MEM;   /* both accounts already used */
    }

    /* Save the new account */
    memset(&admins[free_slot], 0, sizeof(admin_record_t));
    admins[free_slot].used = true;
    strncpy(admins[free_slot].username, username, NAME_MAX_LEN - 1);
    strncpy(admins[free_slot].password, password, PASSWORD_MAX_LEN - 1);
    return ESP_OK;
}

esp_err_t auth_login(const char *username, const char *password)
{
    if (username == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!auth_any_admin_exists()) {
        return ESP_ERR_NOT_FOUND;       /* nobody has run setup yet */
    }

    int64_t now = esp_timer_get_time();
    if (now < locked_until_us) {
        return ESP_ERR_INVALID_STATE;   /* still locked out */
    }

    /* Look for an account where BOTH the name and the password match */
    for (int i = 0; i < ADMIN_COUNT; i++) {
        if (admins[i].used
            && strcmp(username, admins[i].username) == 0
            && strcmp(password, admins[i].password) == 0) {
            logged_in = true;
            current_admin = i;
            last_activity_us = now;
            failed_attempts = 0;
            return ESP_OK;
        }
    }

    /* No match: count the failure, and lock for 30 seconds after 3 */
    failed_attempts++;
    if (failed_attempts >= MAX_FAILED_ATTEMPTS) {
        locked_until_us = now + LOCKOUT_US;
        failed_attempts = 0;
    }
    return ESP_FAIL;
}

void auth_logout(void)
{
    logged_in = false;
    current_admin = -1;
}

bool auth_is_logged_in(void)
{
    if (!logged_in) {
        return false;
    }
    if ((esp_timer_get_time() - last_activity_us) > SESSION_TIMEOUT_US) {
        auth_logout();   /* too long without activity */
        return false;
    }
    return true;
}

const char *auth_current_user(void)
{
    if (auth_is_logged_in()) {
        return admins[current_admin].username;
    }
    return NULL;
}

void auth_note_activity(void)
{
    if (logged_in) {
        last_activity_us = esp_timer_get_time();
    }
}