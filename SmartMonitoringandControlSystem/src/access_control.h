#ifndef ACCESS_CONTROL_H
#define ACCESS_CONTROL_H
#include "system_types.h"
/* Facts are resolved inside controller.c, never trusted from an RFID producer. */
typedef struct {
    bool identity_ready;
    bool identity_known;
    system_user_id_t user_id;
    bool schedule_ready;
    bool schedule_open;
} access_facts_t;
system_result_t access_control_check(const access_facts_t *facts);
#endif
