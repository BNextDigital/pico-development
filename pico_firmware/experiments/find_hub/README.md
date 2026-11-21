# Isolated XIAO Find Hub prototype

This is a separate firmware experiment, version `0.1.0-fhn-probe`, based on
nRF Connect SDK **v3.4.2**'s locator-tag sample. It replaces the companion
application while installed. It does not add Find Hub to the normal Pico image,
and it does not make Pico an approved/certified network accessory.

The build imports Nordic's existing Fast Pair/FHN/DULT application sources from
your unmodified SDK. It replaces DK-only UI, fake battery data and motion
emulation with XIAO adapters. No upstream SDK files or keys are copied into git.
The normal companion firmware, protocol and settings format are unchanged.

## What is implemented

- Existing OLED face renderer; happy face when provisioned, searching face/footer
  while ringing. Serial logs report the individual network/UI state flags.
- Green 200 ms-on/5-second-off heartbeat. Red/blue RGB channels start inactive.
- D0/D1 interrupt-triggered debounce with no idle button polling.
- Real Sense IMU through Pico's adapter when the SDK requests separated-motion
  detection; accelerometer off outside that window. Unavailable IMU fails setup;
  a later sensor failure conservatively reports movement rather than silence.
- Optional existing MAX98357A/I2S wiring for repeating ring indications. Network
  ringing is separate from normal Pico mute settings, which this image never
  loads. Ring volume is a fixed prototype level, not a calibrated/compliant output.
- Battery explicitly reported unavailable. No fabricated 100% charge reading.
- Separate FHN settings and registration partitions, with guarded UF2 packaging.

Still missing: verified Google provisioning/reporting, certification, production
sound-volume/acoustic behavior, real battery reporting, OTA update support and
coexistence with Pico's own GATT/personality/features. IMU thresholds and DULT
behavior require physical validation. Preserve the SDK's unwanted-tracking
behavior; do not distribute this as a finished locator tag.

## Hardware

Use the existing XIAO nRF52840 **Sense**, OLED on D4/D5 and D0/D1-to-GND buttons.
Optional sound uses the existing [audio overlay](../../overlays/xiao_ble_audio.overlay):
D8/P1.13 = BCLK, D9/P1.14 = LRC, D10/P1.15 = DIN to MAX98357A. Use the existing
safe amplifier/speaker power wiring. Never drive the speaker from a GPIO.
No third button, GPS, UWB or magnetometer is assumed.

| Gesture | Prototype action |
| --- | --- |
| D0 tap | Toggle pairing advertisement request; SDK policy still owns provisioned-state rules |
| D1 tap | Identification mode |
| D1 hold at least 3 seconds, release | Recovery mode |
| D0 hold at least 3 seconds, release | Explicit bench motion injection while detector is active |
| Either button while ringing | Stop ringing; consume the gesture |
| Both buttons held at least 5 seconds | Factory-reset **this experiment's** settings/provisioning state |

A two-button chord never falls through into two individual actions. Factory
reset is deliberately unavailable as a single-button tap or hold. Nordic's
factory reset keeps registration/model credentials, but clears the network
owner/provisioning state. Physical deactivation is still required for product
acceptance: disconnecting USB is insufficient if a battery is installed.

## Build-only first

Open an SDK 3.4.2 terminal. From the repository root:

```powershell
python pico_firmware/experiments/find_hub/build.py --nrf-dir C:/ncs/v3.4.2/nrf
```

For the wired amplifier profile, add `--audio`. The helper verifies the SDK tag,
uses a pristine **no-sysbuild/no-MCUboot** build, and checks generated settings
and devicetree partitions. No credentials means no UF2, and runtime Fast Pair
activation is not expected to succeed. This is intentional build/fit testing.
A separately installed compatible Zephyr SDK can use `--external-toolchain`;
this is unnecessary in the normal Nordic toolchain terminal.

## Provisioning and packaging

You need a Google/Nordic-authorized development Model ID, its matching
anti-spoofing private key and any required test-account/console permissions.
The [Google partner process](https://developers.google.com/nearby/fast-pair/landing-page-find-hub)
remains an external gate. A syntactically valid/random key does not enroll Pico
or grant network access. No Google application, account configuration or device
proposal was submitted by this work.

Keep a private JSON file **outside the Pico repository** with two fields:
`model_id` (format `0x` plus six hex digits) and `anti_spoofing_key` (the provided
32-byte private scalar encoded in base64). Do not paste the key into source,
CMake arguments, chat, screenshots or a PR.

```powershell
python pico_firmware/experiments/find_hub/build.py --nrf-dir C:/ncs/v3.4.2/nrf --audio --credentials C:/Users/brayd/PicoPrivate/find-hub.json
```

Only this explicit credentials + audio path creates `build-xiao/pico-find-hub.uf2`.
The file contains private registration data. Keep it private; build directories
are gitignored. POSIX output permissions are owner-only; Windows filesystem ACLs
remain your responsibility. Packaging never flashes or erases a board.

The guarded UF2 is the only intended drag-and-drop artifact. Do **not** flash a
DK image, SDK `merged.hex`, a sample bootloader, or use a mass erase. Hardware
flashing/bootloader acceptance remains pending; retain the known-good companion
UF2 before trying the prototype.

## Flash layout

| Range, end exclusive | Use |
| --- | --- |
| `000000–027000` | Existing SoftDevice/MBR reservation; not written |
| `027000–0E0000` | Isolated application, 740 KiB |
| `0E0000–0E1000` | Dedicated model registration record area, 4 KiB |
| `0E1000–0E9000` | Prototype's runtime FHN/settings NVS, 32 KiB |
| `0E9000–0EC000` | Unused gap |
| `0EC000–0F4000` | Normal Pico settings/bonds, preserved bytes |
| `0F4000–100000` | Factory UF2 bootloader reservation; not written |

Packaging checks the board/config, all generated partition addresses, image
size, UF2 family/sequence and every block address. It writes a contiguous
application + registration area, including erased-byte padding. It cannot
include either settings region, UICR or either bootloader range. The registration
format is regression-checked against the pinned SDK's provisioning tool.

Restoring the ordinary Pico UF2 restores the companion application. Device-side
normal settings bytes are retained, but phone-side bond/cache behavior still
needs testing; reconnecting may need service rediscovery. A larger future normal
firmware image could overwrite the experiment's registration/settings, which
are inside the ordinary application region. This layout is for isolated
experiments, not a permanent combined-product layout.

## Validation and next hardware gates

See [build results](validation.md) for compiler, target memory and checks.
Portable checks require Python and a C++17 compiler:

```sh
sh pico_firmware/experiments/find_hub/tests/run.sh
```

Set `PICO_TEST_NRF_DIR` and use an SDK Python with `intelhex` to include the
optional registration parity test against the pinned Nordic script.

Next, with authorized test credentials and the wired amplifier:

1. Disable the companion app's left-behind monitor while replacing its firmware.
   Review generated layout and package locally. Flash only the guarded UF2;
   verify it boots without changing the factory bootloader or ordinary settings.
2. Check serial setup logs, OLED, both buttons, heartbeat and actual I2S sound.
3. Exercise Fast Pair/FHN provisioning on the authorized phone/account; verify
   provisioned state survives reset and clock synchronization completes.
4. Test ringing timeout/button stop, identification, recovery, factory reset and
   unprovisioning. Verify real IMU start/stop and unwanted-tracking sound paths.
5. Restore the known-good companion image; check settings, bonds and its own
   tracking behavior. Measure current before treating any policy as battery-ready.
6. Only then combine FHN with Pico's custom GATT, including ownership/security,
   advertising identity/rotation, storage and alert priority. An OS network item
   does not automatically expose a location API to Pico's companion app.
