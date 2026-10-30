# Validation

## Checks

- Pure-C controller tests: six suites passed with host compiler warnings as errors.
- Native PlatformIO esp32dev build: PASSED, 30 October 2026.
- Native PlatformIO role1_demo build: PASSED, 30 October 2026.
- Wokwi/physical runtime: not run for this scaffold yet.

The tests cover provider readiness, unknown cards, closed entry/exit, pending
transactions, matched and mismatched passage, duplicate input, inside/outside
invariants, same-lane conflicts, entry/exit interleaving, timeout boundaries,
explicit revocation, cancelled approvals, register capacity, and independent rooms.

## Runtime check after building role1_demo

1. Run the merged role1_demo firmware on a standard ESP32 in Wokwi.
2. Verify the startup banner says role1_demo=1 and identifies synthetic inputs.
3. Observe entry approvals leave the count unchanged until confirmation.
4. Observe duplicate/already-inside/closed-exit/unknown-card denials.
5. Observe Carol's uncompleted admission expire without adding her.
6. Confirm ROLE1 DEMO PASS and the expected final state in README.md.

This runtime check exercises task integration and synthetic event handling. It
is not an RFID, servo, lighting-sensor, authentication or SD-card acceptance test.
Those need additional evidence when each role's real module is implemented.

Both firmware profiles were built with platform espressif32@6.9.0, ESP-IDF 5.3.1,
target esp32. Complete merged images were produced for both profiles.

## Normal-installation rebuild, 30 October

Reproduced both reported failures using /home/emmaa/.platformio/penv/bin/platformio
with the normal PlatformIO core (no PLATFORMIO_CORE_DIR override). Both reported
`Multiple ways to build the same target` for bootloader/esp_efuse_fields.c.o.
Their generated CMake metadata still referred to the isolated framework directory
used during the earlier scaffold verification.

Ran the standard clean target for both environments, then built both with the
normal installation after consolidating the unfinished implementations into
integration_stubs.c. Results: esp32dev SUCCESS (20.1 seconds), role1_demo SUCCESS
(21.4 seconds). The six host-controller suites also passed. No package-version
change was required. This verifies compilation; the runtime checks above remain
outstanding.

After removing the eight feature-specific headers and unused startup/API stubs,
both profiles passed again using the normal installation (esp32dev 19.2 seconds,
role1_demo 19.8 seconds). All six host suites passed. Only the four controller
adapter functions remain in integration_stubs.c; runtime validation is unchanged.
