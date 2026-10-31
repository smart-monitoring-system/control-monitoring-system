# CoCIS Smart Monitoring and Control System

Single ESP32, entrance plus two lecture rooms, native ESP-IDF, free Wokwi simulation.
This is a working Role 1 core and module scaffold, not a completed coursework system.
Mary's 29 October clarification is the basis for using one microcontroller.

## Toolchain

The existing platformio.ini choices are retained:

```ini
platform = espressif32@6.9.0
board = esp32dev
framework = espidf
```

Platform 6.9.0 selects ESP-IDF 5.3.1 and the standard ESP32 target. ESP32-S3 firmware
and pin diagrams are not interchangeable with this target. The configuration is
sufficient for this scaffold; finalize shared-bus and GPIO allocation before wiring
RFID, SD, servos, sensors, and lights. Do not upgrade the framework during integration
without a reason. The existing generated 2 MB flash setting was corrected to
4 MB to match esp32dev, and sdkconfig.defaults supplies that value to fresh builds.
CMake explicitly lists sources instead of recursively compiling
all files under src.

## Build and exercise

From a PlatformIO terminal in this directory:

```sh
pio run -e esp32dev
pio run -e role1_demo
python3 scripts/test_core.py
```

- **esp32dev** is the normal baseline. The controller runs, but providers that are
  not implemented return ESP_ERR_NOT_SUPPORTED. Access is denied until identity,
  schedule, and door integrations are actually connected. The entry MFRC522 scan
  prints the card UID to the serial monitor and submits it to the controller;
  access remains denied while the schedule provider is unavailable. No default
  admin password or permanently open production schedule is installed.
- **role1_demo** compiles synthetic card identities, a controllable fake schedule,
  fake actuator acceptance, and an automatic event sequence. It prints
  `ROLE1 DEMO PASS` only when the final controller snapshot has the expected state.
  No RFID reader, sensor, servo, calendar clock, authentication, or SD persistence
  is being exercised by that demo.
- Host tests exercise the actual pure-C controller, occupancy, and decision code
  without an ESP32. A host C compiler (`cc`, or the CC environment variable) is needed.

`app_main` is the native ESP-IDF entry point. There is no sketch.ino or Arduino
launcher in this project.

If an existing build reports `Multiple ways to build the same target`, especially
with framework paths from a different PlatformIO installation, clean both profiles
once before rebuilding:

```sh
pio run -t clean -e esp32dev -e role1_demo
pio run -e esp32dev -e role1_demo
```

The first scaffold verification used an isolated PlatformIO installation. Its
generated CMake cache was incompatible with the normal installation. Both profiles
have since been cleaned and rebuilt using the normal installation. No framework
upgrade or Arduino conversion is needed.

Each build also creates `.pio/build/<environment>/firmware-merged.bin`, containing
bootloader, partition table, and application at their configured flash offsets.
The plain firmware.bin is only the application. The post-build merger uses the
installed PlatformIO esptool package; there are no network dependencies in firmware.

## Wokwi

The diagram contains the ESP32, RFID readers, servos, LEDs, and serial connections.
The normal firmware currently initializes and polls the entry reader only; it holds
the unused exit reader's chip-select high because both readers share the SPI bus.
The exit reader and servo outputs still need their firmware integration.

- For normal firmware, use wokwi.toml after building esp32dev.
- For the synthetic demonstration, select wokwi.demo.toml with the extension's
  `Wokwi: Select Config File` command after building role1_demo.
- For a browser demonstration, use a standard ESP32 circuit and F1 ->
  `Upload Firmware and Start Simulation...`, selecting the desired environment's
  firmware-merged.bin. The browser's normal build button is a different toolchain;
  do not paste this native project into an Arduino sketch and claim the same build.
- At 115200 baud, the demo should finish after roughly 8-10 simulated seconds.
  The expected final state is Brian (user ID 2) inside, Alice/Carol outside, Room 1
  occupied/light on, Room 2 empty/light off, and no pending access transactions.
- To test the entry reader, build the `esp32dev` profile, start the simulation with
  `wokwi.toml`, open the serial monitor at 115200 baud, and tap a card on the entry
  MFRC522. The monitor prints `RFID ENTRY: UID = ...` once per tap, followed by the
  controller's decision. Remove the card before tapping it again. A `NOT_READY`
  decision means the schedule provider is not active; it does not mean the scan
  failed.

No MQTT broker or multiple tabs are required for the assessed baseline. The old
cross-tab experiment is separate evidence of a possible future distributed design.

## Implemented now

- Copying event queue and one controller task that owns mutable system state.
- Mutex-protected status snapshots; thread-safe local event-ID generation.
- Identity/schedule readiness checks and both-direction access gating.
- Per-user inside list; count derived from membership, never blind increments.
- Approval -> pending passage -> matched confirmation -> membership update.
- Five-second passage timeout, pending cancellation, and doorway conflict checks.
- Bounded duplicate-event detection, invalid-input checks, and register capacity.
- Two independent room status snapshots, separate from building membership.
- Four temporary adapter functions for services the controller already calls.
- Pure-C core tests and a synthetic FreeRTOS demonstration profile.

## Which files matter now?

There is no need to create a separate empty .c file for each future feature.
The current source layout separates working code from unfinished integrations:

| Files | Purpose |
| --- | --- |
| main.c | Start Role 1; optionally start the synthetic demo |
| system_types.h/.c | Shared events, snapshots, and readable result names |
| controller_core.c, occupancy.c, access_control.c | Working Role 1 decisions and state changes; tested on the host |
| controller.c | Working FreeRTOS queue, controller task, and snapshot mutex |
| role1_demo.c | Optional synthetic exercise enabled only in role1_demo |
| integration.h, integration_stubs.c | Four external calls needed by the controller: identity lookup, door command, schedule check, audit enqueue |

For Role 1, read system_types.h, controller_core.c, controller.c, then role1_demo.c.
The other active core .c/.h pairs contain working logic; do not delete them without
updating their callers and CMake. role1_demo.* is optional test code, not required
for the eventual system; keep it while using the role1_demo build.

No auth/config/CLI/room/driver skeletons are imposed on teammates. Each member can
choose their own modules, private types, tasks, and initialization functions. When
integrating, add their sources to src/CMakeLists.txt, initialize their modules from
main.c, and make the four adapters call their APIs. An adapter can translate names
or types; teammates do not need to redesign their code to match these stub names.
Replace stub bodies or move functions, rather than defining each symbol twice.

Agree the shared events in system_types.h and the queue/snapshot API in controller.h
before independent work. Those are the current connection points between roles.
There is no comms module in the one-board design.

## Next integration work

See docs/ROLE_HANDOFFS.md for ownership, event contracts, and first handovers.
See docs/ROLE1_DESIGN.md for synchronization, passage assumptions, and limitations.
See docs/VALIDATION.md for checks actually completed and runtime checks still needed.

Complete the required RFID/sensor/actuator/storage/time/authentication integrations
before describing this as a finished system. Console logging is not persistent
storage, and esp_timer uptime is not a real access-schedule clock.
