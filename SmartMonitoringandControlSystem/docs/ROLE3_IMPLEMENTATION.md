# Role 3: independent directional room control

Each lecture-room doorway has two digital proximity/beam sensors. Sensor A is on
the outside and B is on the inside. Detection zones must overlap during a person's
crossing. Entry is A only, both, B only, then clear; exit is the reverse sequence.
Each room owns its own count, state machine, light output, and report retry. The
module never reads or modifies building membership.

## Pin allocation and wiring

| Function | GPIO | Notes |
| --- | ---: | --- |
| Room 1 doorway sensor A (outside) | 34 | Input-only, external 10 kOhm pull-down required. |
| Room 1 doorway sensor B (inside) | 35 | Input-only, external 10 kOhm pull-down required. |
| Room 1 light/relay driver | 13 | Output; GPIO27 remains the exit passage button. |
| Room 2 doorway sensor A (outside) | 36 | Input-only, external 10 kOhm pull-down required. |
| Room 2 doorway sensor B (inside) | 39 | Input-only, external 10 kOhm pull-down required. |
| Room 2 light/relay driver | 14 | Output. |

All four sensor modules must provide a 3.3 V active-high digital signal. GPIO
34-39 have no internal pull resistors, so the external pull-downs in the Wokwi
diagram are also required in physical wiring. A real light relay requires an
appropriate transistor/relay driver and separate suitable power; never connect a
relay coil directly to an ESP32 pin.

These pins do not conflict with RFID SPI/reset (5, 16, 17, 18, 19, 22, 23),
passage buttons (21, 27), door LEDs (25, 26), serial (1, 3), or Wokwi servo
signals (32, 33).

## State machine

The room task samples every 25 ms and accepts a sensor level only after 50 ms of
stability. It accepts one clean crossing at a time:

```text
Entry: clear -> A only -> both -> B only -> clear -> count + 1
Exit:  clear -> B only -> both -> A only -> clear -> count - 1
```

The room is `OCCUPIED` when its known count is greater than zero and `EMPTY` at
zero. Count changes only after the final clear sample: a pending exit keeps the
existing occupancy and light state. The light is on for OCCUPIED or UNKNOWN and
off only for known EMPTY. The controller receives
an `EVENT_ROOM_STATE` when this public state or the actual light output changes.

## Ambiguity and recovery policy

The module refuses to invent a count. It changes only the affected room to
`UNKNOWN` and keeps its light on if any of these occur:

- both sensors trigger together;
- a complete crossing does not finish within two seconds of its first beam;
- a beam triggers again before a crossing is complete;
- the first beam clears before overlap is observed;
- beams clear in the wrong order, or both clear in one sample hiding the order;
- an exit is reported while the room count is already zero;
- a room counter reaches its configured maximum; or
- the room-controller GPIO output fails.

A turnaround after both beams activate is detected by the wrong release order.
The count is not committed and the affected room becomes UNKNOWN, leaving its
light on. Only observable invalid sequences can be detected: two people whose
signals resemble one valid crossing can still be miscounted. This prototype
assumes one person crosses a room doorway at a time.

After an ESP32 restart, both rooms start `UNKNOWN` with lights on. The board has
no persistent, trustworthy room count, so it cannot assume empty rooms. An
authenticated staff member must physically inspect the room, confirm it is empty
and its doorway is clear, then run:

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

After reconciling a room as empty:

- Entry: hold A, hold B too, release A, then release B.
- Exit: hold B, hold A too, release B, then release A.

Allow about 100-300 ms per step for the input filter, completing the sequence
within two seconds. Separate press-and-release taps do not establish overlap and
are treated as ambiguous. Test reversing the release order, a timeout, and
simultaneous keys to confirm that only the relevant room becomes UNKNOWN and
its light stays on.

Use the normal `esp32dev` profile, not `role1_demo`: the latter deliberately
publishes its own synthetic room events.

## Combined diagram

diagram.json now retains all room-branch components, labels, keyboard controls,
and positions, and adds main's two building passage buttons and their connections.
Both RFID readers, servos, and door indicator circuits remain. The wiring matches
the corrected source:

- Entry RFID reset stays on GPIO22; exit RFID reset stays on GPIO16.
- Entry passage stays on GPIO21; exit passage stays on GPIO27.
- Room 1 light uses GPIO13; it no longer shares GPIO27 with exit passage.
- Room 2 light stays on GPIO14; room sensor pins are 34/35 and 36/39.

If Git flags diagram.json during the later merge, keep this combined feature-branch
version as the resolution. It includes the room implementation and main's passage
controls. The JSON and pin connections have been checked; live Wokwi validation
is still pending.
