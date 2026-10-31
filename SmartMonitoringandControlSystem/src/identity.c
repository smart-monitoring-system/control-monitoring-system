#include <string.h>
#include "identity.h"

/*
 * TEMPORARY WOKWI TEST REGISTRY
 *
 * These UIDs correspond to Wokwi's built-in MFRC522 card presets.
 * Replace these entries with the actual registered users when
 * the identity-management module is integrated.
 */

typedef struct {
    system_uid_t uid;
    system_user_id_t user_id;
} registered_card_t;

static const registered_card_t registered_cards[] = {
    {
        .uid = { .length = 4, .bytes = {0x01, 0x02, 0x03, 0x04} },
        .user_id = 1
    },
    {
        .uid = { .length = 4, .bytes = {0x11, 0x22, 0x33, 0x44} },
        .user_id = 2
    },
    {
        .uid = { .length = 4, .bytes = {0x55, 0x66, 0x77, 0x88} },
        .user_id = 3
    },
    {
        .uid = { .length = 4, .bytes = {0xAA, 0xBB, 0xCC, 0xDD} },
        .user_id = 4
    }
};

esp_err_t identity_lookup_user(
    const system_uid_t *uid,
    system_user_id_t *user,
    bool *known
)
{
    if (uid == NULL || user == NULL || known == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *user = 0;
    *known = false;

    if (uid->length == 0 || uid->length > SYSTEM_UID_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t card_count =
        sizeof(registered_cards) / sizeof(registered_cards[0]);

    for (size_t i = 0; i < card_count; i++) {

        if (uid->length == registered_cards[i].uid.length &&
            memcmp(
                uid->bytes,
                registered_cards[i].uid.bytes,
                uid->length
            ) == 0) {

            *user = registered_cards[i].user_id;
            *known = true;

            return ESP_OK;
        }
    }

    /* An unregistered card is not a provider failure. */
    return ESP_OK;
}