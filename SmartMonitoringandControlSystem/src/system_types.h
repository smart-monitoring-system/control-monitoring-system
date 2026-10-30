#ifndef SYSTEM_TYPES_H
#define SYSTEM_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SYSTEM_MAX_USERS 32
#define SYSTEM_UID_MAX_BYTES 10
#define SYSTEM_ROOM_COUNT 2
#define SYSTEM_EVENT_QUEUE_LENGTH 16
#define SYSTEM_RECENT_EVENT_COUNT 32
#define SYSTEM_PASSAGE_TIMEOUT_MS 5000U

typedef uint32_t system_user_id_t; /* 0 is invalid. */
typedef uint32_t system_event_id_t; /* Non-zero, unique for this controller boot. */
typedef enum { DIRECTION_ENTRY = 0, DIRECTION_EXIT = 1 } system_direction_t;
typedef enum { ROOM_UNKNOWN = 0, ROOM_EMPTY, ROOM_OCCUPIED } room_occupancy_t;
typedef struct { uint8_t length; uint8_t bytes[SYSTEM_UID_MAX_BYTES]; } system_uid_t;
typedef struct { bool known; room_occupancy_t occupancy; bool light_on; } room_status_t;

typedef enum {
    RESULT_IDLE = 0, RESULT_ACCESS_PENDING, RESULT_ENTERED, RESULT_EXITED,
    RESULT_ROOM_UPDATED, RESULT_DUPLICATE_EVENT, RESULT_UNKNOWN_USER,
    RESULT_NOT_READY, RESULT_SCHEDULE_CLOSED, RESULT_ALREADY_INSIDE,
    RESULT_NOT_INSIDE, RESULT_CAPACITY, RESULT_BUSY, RESULT_UNEXPECTED_PASSAGE,
    RESULT_PASSAGE_EXPIRED, RESULT_INVALID_EVENT, RESULT_DOOR_UNAVAILABLE, RESULT_ACCESS_REVOKED
} system_result_t;

typedef enum { EVENT_ACCESS_SCAN = 0, EVENT_PASSAGE_CONFIRMED, EVENT_ROOM_STATE, EVENT_REVOKE_PENDING } system_event_type_t;
typedef struct {
    system_event_id_t id;
    system_event_type_t type;
    union {
        struct { system_uid_t uid; system_direction_t direction; } scan;
        /* transaction_id is the original accepted SCAN event ID. */
        struct { system_event_id_t transaction_id; system_direction_t direction; } passage;
        struct { uint8_t room_id; room_occupancy_t occupancy; bool light_on; } room;
    } data;
} system_event_t;

typedef struct {
    system_user_id_t users[SYSTEM_MAX_USERS];
    size_t count;
} occupancy_state_t;
typedef struct {
    bool active;
    system_user_id_t user_id;
    system_event_id_t transaction_id;
    uint64_t deadline_ms;
} pending_passage_t;
typedef struct {
    occupancy_state_t building;
    pending_passage_t pending[2];
    room_status_t rooms[SYSTEM_ROOM_COUNT];
} system_snapshot_t;
typedef struct {
    system_event_id_t event_id;
    uint64_t uptime_ms; /* Monotonic uptime, NOT calendar time. */
    system_result_t result;
    system_user_id_t user_id;
    size_t inside_count;
    bool has_source_event;
    system_event_t source_event; /* UID, lane, transaction, or room data for storage. */
} system_audit_record_t;
const char *system_result_name(system_result_t result);
#endif
