# Parking Recall

Open **Parking** from the root menu with the Navigation button. Use the Action
button to enter, then tap it again on **Park Here** while safely parked and the
Android companion is open. Hold Action or Navigation to go back, including
cancelling a pending request. The first press on an asleep OLED only wakes it.
A successful phone commit briefly shows a happy face, then a **SAVED** card.
A further Action tap can capture a replacement. No audio is required.

Parking uses the former Wayfinding menu position. An explicit utility screen
leaves the current mode and any active Focus session intact. The Wayfinding
mode ID remains 4. Physical dock state remains solely a hardware observation.
A docking event never initiates a save, and entering a physically detected car
dock cancels a pending parking request. Timer, Clock alarm and local finding
overlays retain their existing display and input priority.

## Protocol v1 extension

Primary status remains 16 bytes and advertises Parking in capability bit 10.
The one new characteristic is
`8e7b0007-54f2-4d9b-a3d1-9f150000c001`, with authenticated Read and Notify
and an authenticated CCC. Existing authenticated command characteristic 0003
carries two new commands. Combined firmware restricts parking to the
companion identity. The nine-byte packets fit the default ATT MTU.

| Packet | Byte 0 | Byte 1 | Bytes 2–5 | Bytes 6–8 |
| --- | --- | --- | --- | --- |
| Parking status | Extension version `1` | Phase | Boot ID, uint32 LE | Request sequence, uint24 LE |
| Client readiness | `0x70` | `0` unavailable, `1` ready | | |
| Request result | `0x71` | Result phase | Matching boot ID, uint32 LE | Matching request sequence, uint24 LE |

| Value | Phase | Ownership |
| --- | --- | --- |
| 0 | Idle | Firmware |
| 1 | Saving | Firmware, local Action tap |
| 2 | Saved | Phone, after durable commit |
| 3 | Open app | Firmware when phone is unavailable, or phone when suspended |
| 4 | No location | Phone |
| 5 | Failed | Phone |
| 6 | Cancelled | Either |
| 7 | Timed out | Firmware |
| 8 | Link lost | Firmware |
| 9 | Allow location | Phone |

The boot ID is a nonzero volatile random generation on the board. The counter
is bounded to 24 bits and skips zero. Sequence 0 means there is no actionable
request. Readiness is link-local and is cleared on connection changes. The app
subscribes and reads status during authenticated setup, then indicates when it
can handle a request. A connected old app without readiness receives no
request, and Pico shows **OPEN APP** immediately.

Only an Action-button tap on the Parking screen can create a request. Remote
menu selection can open the Parking screen. One request can be active at a time.
The existing monotonic one-second service tick expires it after 45 seconds.
A held back action cancels it. A disconnect ends any pending operation. A
phone that already committed the matching record may report **Saved** for the
same ID after disconnection or expiration. Cancellation and newer IDs invalidate
old results. Firmware does not write parking state or coordinates to flash.

Status is cached and readable after a lost notification. The app reads on
attachment and foreground return, and verifies the matching pending request
before writing. No parking packet contains coordinates, location time or notes.

## Software checks

```sh
sh pico_firmware/tests/host/run.sh
sh pico_firmware/simulator/run-tests.sh
pico_firmware/build-host/ble_recovery_tests --parking-fixture
```

The fixture exercises the production GATT adapter and codecs. The simulator
exposes `parking_phase`, `parking_hex` and `parking_simulated` in its JSON
state. Exercise it with `event Connected`, `ble 7001`, the menu/button
events and a matching result. A simulated acknowledgement tests firmware
behavior and does not capture or persist a real location.

## Physical acceptance

1. Flash the standard XIAO UF2 using the factory USB bootloader. Confirm normal
   OLED startup and connect using the companion. Use the local Pair Phone
   window and the OLED passkey if a new Android bond is required.
2. While parked with the app foregrounded, open Parking and tap Action twice
   from the selected root icon. Observe **SAVING**, then the happy face and
   **SAVED**. In **Find → Find My Car**, check the saved time, accuracy and
   approximate position. Test with the OLED asleep first and confirm that the
   wake press alone does not send a request.
3. Revoke location permission and repeat. Expect **ALLOW GPS** with the prior
   record retained. Disable location services and expect **NO LOCATION**.
   Disconnect before the tap for **OPEN APP**. Disconnect during capture for
   **LINK LOST**. Reconnect and verify that only a previously committed
   matching request can reconcile to **SAVED**.
4. Check that holding Back cancels, an unanswered request reaches **TIMED OUT**,
   and a later physical retry gets a new ID. Run a Focus session and trigger a
   Clock alarm or Call Pico during Parking to verify their existing behavior.
5. On optional combined firmware, verify the same companion behavior with
   Find Hub network activation disabled. Use the Pico-only UF2 for rollback.

Build with NCS 3.4.2 using the standard and combined profiles documented in
[Combined Find Hub](combined-find-hub.md). Parking adds no partitions, settings
keys, threads, drivers or runtime location buffers on Pico.
