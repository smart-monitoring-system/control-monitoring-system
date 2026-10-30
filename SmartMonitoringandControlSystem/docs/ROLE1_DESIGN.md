# Role 1 design and boundaries

## State ownership

controller_core.c is a pure-C state machine. controller.c surrounds it with a
FreeRTOS event queue and one writer task. Producer tasks submit copied inputs.
Readers take a bounded mutex-protected copy of system_snapshot_t. Peripheral and
storage calls occur after releasing the snapshot lock to avoid holding it across
external I/O. Producers must not call occupancy_enter()/exit() on shared state.

The pure core accepts resolved identity/schedule facts from controller.c. Those
facts are not accepted as fields from external producers. The normal providers
currently return NOT_SUPPORTED, so the production profile cannot grant access.
The demo profile resolves only its own synthetic fixture IDs and fake schedule.

## Passage transaction

1. Read a card on a designated entry or exit reader.
2. Resolve identity and obtain the schedule decision for that direction.
3. Reject unknown/unavailable/closed/invalid membership cases.
4. Reserve the lane and user in a pending transaction, lasting five seconds.
5. Request the corresponding door opening. On actuator failure cancel approval.
6. A passage input with matching lane and original transaction ID commits entry
   or exit exactly once and requests door closure.
7. Missing/late confirmations expire without changing membership.

Timeouts run even when the queue is idle and are checked before every event.
A callback must not block the controller; unfinished hardware returns an error.
The demonstration substitutes a successful logged actuator request, not a servo.

The core is a controlled-passage model: one lane transaction and one person per
accepted passage. It does not detect tailgating, physical obstruction, or an
uncommanded crossing. A real sensor confirmation records a crossing; room/building
counts must not be presented as physically infallible. Door feedback and recovery
policy remain integration work.

## Schedule changes

The initial decision checks both directions, including the coursework's explicit
exit restriction. Role 4 must send EVENT_REVOKE_PENDING when the schedule/time
validity changes so an old approval does not survive a newly closed window.
New requests re-read provider policy. A late confirmation after revocation is
rejected in this simulation model. The demo schedule command exercises this path
through the same pure core; no management authentication is implemented by it.

## Capacity and duplicate scope

SYSTEM_MAX_USERS=32 is a configurable RAM-register limit for this scaffold, not
an assigned physical capacity of the building. Entry at that limit is denied.
The current two lanes allow at most one pending entry and one pending exit, with
one pending transaction per user. Count changes only on confirmed membership
transitions. Rooms maintain their own independent status.

Event IDs are local to one firmware boot. The recent-event cache holds 32 IDs;
it is bounded duplicate protection, not a persistent exactly-once transaction
protocol. Continuous-card filtering is also required in Role 2. Accepted retry
IDs should remain stable. New logical events must receive fresh IDs.

## Persistence and hardware

The register is RAM-only and starts empty. Storage currently returns NOT_SUPPORTED;
console output does not survive restart. Real time, authorized configuration,
identity registry, drivers, room sensing and SD recovery must be integrated before
an actual admission-ready system is claimed. The demo is disabled by default and
contains no live hardware accesses, password acceptance or real schedule clock.

Native build entry point is app_main(). Do not add the previous browser Arduino
launcher to this PlatformIO ESP-IDF project.
