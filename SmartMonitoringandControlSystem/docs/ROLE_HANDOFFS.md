# Team handovers

## Shared rules

1. One controller, one diagram, entrance plus two independently modelled rooms.
2. Only the controller task changes building membership and pending access state.
3. Producers call controller_next_event_id() and controller_post_event(); events
   are copied. Keep the same event ID only when retrying the SAME logical event.
4. If posting times out, report/backpressure/retry; never silently discard an
   entry/exit or invent a successful admission. Do not block in an interrupt.
5. Snapshot readers call controller_get_snapshot() and receive a consistent copy.
6. No module may report success for hardware or storage work it has not performed.
7. Agree GPIO and SPI initialization ownership before wiring. RFID/SD can require
   shared-bus coordination; unique chip-selects and appropriate locking matter.

## Where unfinished code lives

There are no pre-created module files for Roles 2-5. Each member chooses their
own files, tasks, private data, initialization and driver APIs. Add their real
sources to src/CMakeLists.txt and startup calls to main.c when integrating.

src/integration.h lists only the four services the current controller calls:
identity lookup, door command, schedule check, and audit enqueue. Their temporary
implementations in integration_stubs.c return NOT_SUPPORTED. Adapt these bodies
to call teammates' APIs; their internal names and architecture can differ. If an
owner provides the exact symbol directly, remove its stub to avoid two definitions.

Agree system_types.h events and controller.h queue/snapshot calls as a team. These
shared inputs/outputs matter for integration; the proposed module filenames do not.

## Role 1: controller, occupancy, integration

Files: system_types.*, controller.*, controller_core.*, occupancy.*, access_control.*.

Implemented: queue/task ownership, snapshots, transaction-based passage, duplicate
checks, timeouts, both-direction policy checks, and room state aggregation.

First integration: connect Role 2 identity lookup and door commands, then Role 4
schedule checks. Scan approval does not immediately add someone to the register.
A physical/test passage confirmation commits the change. Test failure and late
confirmation paths before adding presentation polish.

## Role 2: identity and doors

Choose the RFID/door implementation and startup functions. Connect identity lookup
and door commands through integration.h when integrating.

- EVENT_ACCESS_SCAN payload: UID length/bytes and ENTRY or EXIT direction.
- Registry lookup: ESP_OK plus known=false is an unknown card; provider failures
  return an error so the controller does not treat unavailable lookup as approval.
- door_set_open() must return quickly; control timers/PWM without long task sleeps.
- A pending transaction is visible in controller_get_snapshot().pending[direction].
  A passage confirmation must carry its original scan transaction_id and direction.
- Inputs without an active authorized transaction do not change occupancy.
- RFID identifies a tag, not actual passage. The simulation can initially use a
  labelled passage button, explicitly documented as a sensor input substitute.
- Continuous tag presence must not create uncontrolled streams of fresh scans.

## Role 3: both rooms

Choose the room module's startup function and use two instances of the room logic.

Publish EVENT_ROOM_STATE with room_id 1 or 2, occupancy (UNKNOWN/EMPTY/OCCUPIED),
and the actual light output state. The room module owns sensor interpretation and
local lighting; the controller stores reported status. Building membership is not
an input that automatically turns both room lights on.

Define the occupancy method before coding: PIR timeout is an estimate; doorway
counting requires handling reversals and missed/overlapping crossings. Publish
UNKNOWN on sensor uncertainty/failure instead of claiming verified empty.

## Role 4: schedules, auth, configuration, CLI

Choose the schedule, authentication, configuration, and CLI APIs using native
ESP-IDF. Only the controller-facing schedule check is declared in integration.h;
the rest of this role's interfaces are left to its owner.

- schedule_access_allowed() must gate ENTRY and EXIT and indicate unavailable time
  or configuration with an error. Do not derive wall-clock hours from uptime.
- Define timezone, opening/closing inclusivity, and overnight-window behavior.
- Only authenticated authorized sessions can execute schedule changes; a card's
  registration does not grant administrative permission.
- Keep the configured schedule available to management outside opening hours.
- Save/apply a complete validated schedule, not separately visible half-updates.
- When a schedule closes, becomes invalid, or is edited so outstanding approvals
  no longer apply, post EVENT_REVOKE_PENDING before accepting further passage.
  That event cancels all uncompleted approvals and requests door closure.
- The coursework requires exit restriction outside the permitted interval.
- The CLI uses snapshots and storage retrieval. It must not write occupancy arrays.
- Expected commands: help, status, inside, rooms, logs, login, logout,
  schedule show/set. Command parsing and sessions are not implemented yet.

## Role 5: storage and reliability

Choose storage initialization and retrieval APIs with the CLI owner. Connect the
controller's audit enqueue through integration.h.

- Queue record copies to a single SD writer. Do not do synchronous disk writes
  while the controller holds its state mutex or from an interrupt.
- Implement bounded reading for serial queries, full/error conditions, and reporting.
- Audit timestamps currently contain uptime only. Add real timestamps when Role 4
  establishes a valid clock, while retaining event identity and ordering.
- Restore membership/configuration consistently after restart before enabling
  admission; the current Role 1 RAM state does NOT provide restart persistence.
- Define recovery for missing/corrupt records, queued-but-unwritten events, and SD
  failure. Do not claim a persisted event just because it appeared in the console.
- No comms/MQTT module is required for the one-board baseline.

## Producer examples

```c
system_event_t scan = {
    .id = controller_next_event_id(),
    .type = EVENT_ACCESS_SCAN,
    .data.scan = {
        .uid = { .length = 4, .bytes = { 1, 2, 3, 4 } },
        .direction = DIRECTION_ENTRY
    }
};
esp_err_t err = controller_post_event(&scan, pdMS_TO_TICKS(100));
/* Handle err; do not assume queued == admitted. */
```

The scan's ID becomes the transaction ID ONLY if the controller approves it.
Confirm the active transaction, not an arbitrary remembered or last-seen scan.
The public event type never carries an externally supplied "authorized=true" flag:
controller.c resolves identity and schedule through the configured providers.
