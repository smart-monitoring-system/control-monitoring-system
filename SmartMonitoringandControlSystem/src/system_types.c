#include "system_types.h"
const char *system_result_name(system_result_t r)
{
    static const char *const names[] = {
        "idle", "access pending", "entered", "exited", "room updated",
        "duplicate event", "unknown user", "integration not ready", "schedule closed",
        "already inside", "not inside", "capacity reached", "door/user busy",
        "unexpected passage", "passage expired", "invalid event", "door unavailable", "pending access revoked"
    };
    return (unsigned)r < sizeof(names)/sizeof(names[0]) ? names[r] : "invalid result";
}
