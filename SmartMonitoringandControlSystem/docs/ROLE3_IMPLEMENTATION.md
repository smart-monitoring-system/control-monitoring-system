# Role 3: independent directional room control

Each lecture-room doorway has two digital proximity/beam sensors. Sensor A is on
the outside and B is on the inside. A then B is an entry; B then A is an exit.
Each room owns its own count, state machine, light output, and report retry. The
module never reads or modifies building membership.

## Pin allocation and wiring

| Function | GPIO | Notes |
| --- | ---: | --- |
| Room 1 doorway sensor A (outside) | 34 | Input-only, external 10 kOhm pull-down required. |
| Room 1 doorway sensor B (inside) | 35 | Input-only, external 10 kOhm pull-down required. |
| Room 1 light/relay driver | 27 | Output. |
| Room 2 doorway sensor A (outside) | 36 | Input-only, external 10 kOhm pull-down required. |
| Room 2 doorway sensor B (inside) | 39 | Input-only, external 10 kOhm pull-down required. |
| Room 2 light/relay driver | 14 | Output. |

All four sensor modules must provide a 3.3 V active-high digital signal. GPIO
34-39 have no internal pull resistors, so the external pull-downs in the Wokwi
diagram are also required in physical wiring. A real light relay requires an
appropriate transistor/relay driver and separate suitable power; never connect a
relay coil directly to an ESP32 pin.

These pins do not conflict with RFID SPI (5, 17, 18, 19, 22, 23), door LEDs
(25, 26), serial (1, 3), or Wokwi servo signals (32, 33).

## State machine

The room task samples every 25 ms and accepts a sensor level only after 50 ms of
stability. It accepts one clean crossing at a time:

```text
IDLE -- A rising --> WAIT_FOR_B -- B rising --> count + 1 --> WAIT_CLEAR
IDLE -- B rising --> WAIT_FOR_A -- A rising --> count - 1 --> WAIT_CLEAR
WAIT_CLEAR -- both beams low --> IDLE
```

The room is `OCCUPIED` when its known count is greater than zero and `EMPTY` at
zero. The corresponding light is on only while occupied. The controller receives
an `EVENT_ROOM_STATE` when this public state or the actual light output changes.

## Ambiguity and recovery policy

The module refuses to invent a count. It changes only the affected room to
`UNKNOWN` and keeps its light on if any of these occur:

- both sensors trigger together;
- the second expected beam does not arrive within two seconds;
- a beam triggers again before a crossing is complete;
- beams do not clear after a completed crossing;
- an exit is reported while the room count is already zero;
- a room counter reaches its configured maximum; or
- the room-controller GPIO output fails.

This handles a person turning back, stopping between sensors, close/tailgating
people, and missed beams safely. Two beam sensors alone cannot distinguish all of
those cases, so marking the result unknown is more correct than turning lights off
or claiming a wrong count.

After an ESP32 restart, both rooms start `UNKNOWN` with lights on. The board has
no persistent, trustworthy room count, so it cannot assume empty rooms. An
authenticated staff member must physically inspect a clear doorway, then run:

```text
rooms reconcile 1
rooms reconcile 2
```

The command is queued to the room task; it sets the checked room to known empty
only if both doorway beams are clear. It does not change building occupancy.

## Wokwi validation

Wokwi does not provide an appropriate directional proximity-beam component. The
four labelled pushbuttons are therefore deliberate active-high beam simulators:

| Room | A / outside | B / inside |
| --- | --- | --- |
| Room 1 | key `1` | key `2` |
| Room 2 | key `3` | key `4` |

After reconciling a room as empty, press and release A then B to simulate an
entry; press and release B then A to simulate an exit. Release both buttons
before the next crossing. Test a timeout, simultaneous keys, and a second key
before clearance to confirm that only the relevant room becomes `UNKNOWN`.

Use the normal `esp32dev` profile, not `role1_demo`: the latter deliberately
publishes its own synthetic room events.
