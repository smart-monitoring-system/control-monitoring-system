#include "access_control.h"
system_result_t access_control_check(const access_facts_t *f)
{
    if (!f || !f->identity_ready) return RESULT_NOT_READY;
    if (!f->identity_known) return RESULT_UNKNOWN_USER;
    if (!f->user_id) return RESULT_INVALID_EVENT;
    if (!f->schedule_ready) return RESULT_NOT_READY;
    if (!f->schedule_open) return RESULT_SCHEDULE_CLOSED;
    return RESULT_ACCESS_PENDING;
}
