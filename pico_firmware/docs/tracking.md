# Find Pico: locating service and network feasibility

Source version `0.13.0-locator`. This delivery implements own-phone locating
and the companion app's Android presence/last-contact support. No Google Find
Hub or Apple Find My traffic, provisioning keys or GPS is added to this image.

## Firmware behavior

An encrypted Start Finding command begins a bounded 1–120-second session
(the app requests 30). The face shows a curious searching expression and
PICO HERE footer. Existing interaction chirps repeat every two seconds using
the optional audio/buzzer adapter. Mute and zero volume are respected; without
sound hardware the display still works. The display wakes and stays awake.
Timer/Focus timing continues, and timer completion ends locating and takes
alert priority. Repeated starts cannot prolong an active call.

Stop, timeout or a physical-style button ends the session. Button acknowledgement
shows happiness and consumes the press, without selecting a hidden menu item,
petting counter increment or starting/cancelling a timer. The previous screen
is restored. Music/tuner/reference are stopped on entry and do not automatically
resume. An explicit mode/menu/settings/music/dock command exits locating first.
The call survives a BLE disconnect until its timeout and never persists across
reboot. No locating event requests a flash write.

`Locator` is a portable deadline service using unsigned elapsed milliseconds,
including rollover handling and skipped stale pulses. It contributes pulse/end
deadlines to the existing service scheduler; no polling loop is added. The
renderer and simulator use the same core. The next ordinary personality update
restores autonomous behavior after timeout or acknowledgement. The current cue
is a short interaction chirp; this is not yet a measured high-volume finder.

## Additive protocol

Old command/mode IDs, the v1 16-byte status and storage-v5 bytes stay unchanged.

| Wire item | Meaning |
| --- | --- |
| Capability bit 8 / `0x0100` | Portable locating service available; does not imply speaker, GPS or network enrollment |
| `50 ss ss` | Start Finding, uint16 LE seconds, 1–120; exact three-byte packet |
| `51` | Stop Finding; exact one-byte packet |
| Locator characteristic `8e7b0004-54f2-4d9b-a3d1-9f150000c001` | Encrypted read/notify, separate four-byte status |

Locator status: byte 0 version=1; byte 1 flags (bit 0 active, bit 1 sound
available/unmuted/nonzero volume); bytes 2–3 remaining seconds uint16 LE.
Remaining time is refreshed on service passes and published at pulse/start/end
or other notification events, rather than every display frame. The sound flag
reports capability/settings, not measured acoustic output. CCC access requires
encryption. Companion app 0.2.0 discovers this characteristic optionally and
disables Call on older firmware. Old apps can ignore the added capability.

The existing Just Works BLE bonding/encrypted GATT policy remains. This is not
exclusive owner provisioning or MITM-authenticated ownership. Product-grade
ownership/pairing windows, address/beacon privacy and unwanted-tracking handling
must be designed before general distribution or crowd-network integration.
Do not advertise this prototype as Find Hub/Find My compatible.

## Phone responsibilities

The separate companion app implements approximate connected RSSI guidance,
Android companion-device association/presence, delayed left-behind/reunion
notifications and locally persisted last contact/dated phone location. Location
is never sent to Pico. Phone coordinates are historical observations; neither
a current Pico fix nor an exact distance/direction. No cloud backend is needed.
See [companion tracking guide](https://github.com/BNextDigital/pico-companion/blob/feature/locator/pico-companion/docs/tracking.md).
Android's optional all-time location permission only enables cached/passive
background fixes. Without it, alerts still work but coordinates may be absent
or older. Android timers/presence are subject to OS delays and permission/radio
state; they do not guarantee theft recovery or continuous coverage. iOS has no
background tracking adapter in this delivery.

## Find Hub feasibility: SDK 3.4.2 source audit

The pinned [locator sample manifest](https://github.com/nrfconnect/sdk-nrf/blob/v3.4.2/samples/bluetooth/fast_pair/locator_tag/sample.yaml)
explicitly lists `nrf52840dk/nrf52840` for both integration and allowed targets.
This resolves the chip-support uncertainty for **our pinned SDK**, not the
XIAO board port or Pico's combined memory fit. Newer development documentation
must not be substituted for that version's support table.

The pinned [sample README](https://github.com/nrfconnect/sdk-nrf/blob/v3.4.2/samples/bluetooth/fast_pair/locator_tag/README.rst)
describes provisioning, clock synchronization after reboot, DULT identification,
separated-motion ringing, reference hardware with speaker/motion/battery, and
MCUboot-based DFU on the nRF52840 DK. Its application-specific configurations
and UI cannot be copied directly over Pico's factory UF2 bootloader layout.

Next feasibility gates, in order:

1. Build the unmodified pinned sample for its supported nRF52840 DK in a
   separate build directory; record flash/RAM and configuration. **Do not flash
   a DK image or sample bootloader onto the XIAO.** No such build ran here.
2. Audit sample license/components, debug provisioning limitations and Google's
   [current partner process](https://developers.google.com/nearby/fast-pair/landing-page-find-hub).
   Product distribution requires approval, registration, testing/certification
   and activation; no application has been submitted on the user's behalf.
3. Prove an isolated XIAO port with preserved bootloader/settings partitions,
   key storage, battery reporting, real sound, IMU and physical deactivation.
4. Measure fit and coexistence: Pico custom GATT plus Fast Pair/FHN advertising,
   security, identity/address rotation, storage, recovery and bounded audio.
5. Implement/test the network's ownership, DULT and sound requirements. Ordinary
   runtime mute must not silently disable mandated unwanted-tracking indications.
6. Measure current/advertising duty cycle on battery before selecting production
   intervals or estimating life. Existing bring-up advertising/diagnostics remain
   unchanged in this first own-phone revision.

The [Apple route](https://developer.apple.com/find-my/) requires MFi resources
and is a separate feasibility/approval track. Neither network enrollment,
certification cost nor approval time has been established for Pico.

## Validation and hardware acceptance

Passed locally: host suite and Nordic integer-type compile regression; adapter
models; all 33 simulator tests; ASan/UBSan core run with leak detection disabled;
face asset regeneration and whitespace checks. New tests cover packet bounds,
rollover/deadlines, no repeated-call extension, mute/no-speaker status, wake,
acknowledgement, timer priority, disconnected timeout and reboot clearing.

Use your existing SDK 3.4.2 OLED/mic/motion/audio build command with `-p always`
and flash its `zephyr/zephyr.uf2`; keep bring-up diagnostics. No erase/settings
reset is required. On the updated phone app:

1. Discover/read/subscribe to both old status and new locator status while bonded.
   Refresh GATT discovery if the phone retains an old service cache.
2. Call awake/asleep Pico, stop, acknowledge each button, disconnect during a
   call and wait for timeout. Verify muted/zero-volume/no-speaker fallback.
3. Complete a short timer during locating; verify the completion alert wins.
4. Check settings previews and saved settings survive; no hidden control fires
   on acknowledgement. Test 1 and 120-second bounds and repeat-start rejection.
5. Follow companion Android acceptance for association, absence/reunion, Doze,
   reboot, radio toggles, permission loss and dated location data.

No SDK target build, full APK build, physical BLE/OLED/audio/power result,
network enrollment or crowd locating is claimed by this delivery.

## Combined image measurement milestone

The optional [combined profile](combined-find-hub.md) has a locally linked ARM image
with OLED, PDM microphone, IMU and I2S retained. It includes dormant SDK Fast Pair/FHN/
DULT code for honest footprint measurement, identity isolation and runtime profiling.
It does not enroll, advertise on the network or supply qualified network ringing.
See the report for partition verification, standalone comparisons, rollback, outstanding
live arbitration/security work and physical validation gates. Existing Call Pico remains
available through the companion protocol.
