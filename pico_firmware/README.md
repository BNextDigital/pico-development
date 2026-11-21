# Pico firmware

Offline-first Zephyr/C++17 firmware for Pico, a small BLE companion with expressive 128×64 monochrome face, contextual docks and local tools. Target hardware: **Seeed Studio XIAO nRF52840 Sense** with **nRF Connect SDK 3.4.2**.

**Snapshot (November 19, 2026):** the source build-version default is `0.14.2-pin-pairing` (`Kconfig` and `VERSION`). This is a software milestone, **not a hardware-validated release**. Earlier XIAO OLED, BLE and D0 behavior were physically demonstrated; both XIAO profiles have linked locally. Physical passkey pairing and combined-profile acceptance remain pending.

Pairing: [local six-digit passkey flow](docs/pin-pairing.md), with a [separate companion-bond reset](docs/ble-recovery.md). Older companion bonds require a one-time re-pair.

## Implemented software

| Capability | Current implementation / boundary |
| --- | --- |
| Personality | Eight layered bitmap faces, blink/idle behavior and guarded reactions |
| Controls | D0 Action, D1 Navigate, optional D2 Back; icon root menu; GPIO interrupts with deferred debounce/hold |
| Time | Reusable pause/resume timer, Cooking/general timer, Focus goals, Work/Break confirmation, since-boot history and one-minute editors |
| Music | Visual metronome, optional Sense PDM chromatic tuner and E2–C6 reference pitches through optional I2S audio |
| Motion | Optional Sense IMU pickup/rock/rest classification and personality behavior mask |
| Audio | Optional MAX98357A I2S sound cues, volume/mute and visual-only fallback |
| Settings | Thirteen menu items: theme, mute, volume, work/timer, break, motion, test sound, brightness, screen time, sleep now, back, Pair Again and Pair Phone |
| OLED power | 5–100% brightness, Off/1–30-minute inactivity timeout, wake-only first input, timer-alert wake |
| Persistence | 32-byte **storage v5**, migration from v1–v4, coalesced saves; transient timers and editors not restored |
| BLE | Authenticated LE Secure Connections custom GATT protocol v1, 16-byte status, additive commands; bonds stored separately |

Optional peripherals have hardware adapters and host coverage, but **audio/acoustic accuracy, OLED contrast, button interrupts, sensor thresholds and current draw need real-board validation**. Wayfinding GPS/compass, Notes recording, complete dock electronics and final battery/power hardware are not implemented.

## Find Pico

[Local locating and tracking](docs/tracking.md) adds bounded Call Pico,
button acknowledgement and a separate encrypted locator status. The companion
app adds own-phone last contact/location and Android separation alerts. Find
Hub feasibility is audited against SDK 3.4.2; crowd tracking is not integrated.

## Standalone Find Hub bench experiment

The [standalone XIAO experiment](experiments/find_hub/README.md) uses the pinned
SDK locator-tag application with Pico OLED, button, IMU and optional I2S adapters.
It has separate storage and guarded image packaging. Flashing it replaces the
companion application until the normal Pico image is restored. The
[validation notes](experiments/find_hub/validation.md) record build results and
the remaining hardware and authorized provisioning checks.

## Pocket display and controls

[The pocket UI update](docs/pocket-ui.md) gives apps a large value and one
readable selected action, removes face-corner status dots, makes Home Action
pet-only, and changes the XIAO heartbeat to green (200 ms on / 5 seconds off).

## Focus sessions

[Focus sessions](docs/focus-sessions.md) adds a local round goal, progress and
four-entry history explicitly labelled Since Boot. This integration preserves
the separate Focus feature and its tests.

## Music reference pitches

Music -> Reference adds note/frequency selection and two-second E2–C6 reference
pitches, defaulting to A4=440. Sound requires optional I2S hardware; OLED-only
builds show Speaker Unavailable. See [controls, limits and validation](docs/music-reference.md).

## Repository map

| Path | Purpose |
| --- | --- |
| `lib/pico_core/` | Allocation-aware portable personality, utilities, BLE codec, display/persistence |
| `src/platform/`, `src/main.cpp` | Zephyr device adapters and deadline-driven main loop |
| `overlays/`, `configs/` | Explicit XIAO board peripheral profiles |
| `simulator/` | Desktop virtual hardware running the production core |
| `tests/host/`, `tests/platform/` | Regression and adapter-model test sources |
| `docs/` | Architecture, first flash, protocol and feature validation guides |

## Build and flash

Open an **nRF Connect SDK 3.4.2 terminal** in `pico_firmware/`. Start with the OLED-only profile (optional D1 may remain disconnected):

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao . -- -DDTC_OVERLAY_FILE=overlays/xiao_ble_ssd1306.overlay -DEXTRA_CONF_FILE=configs/xiao_ble.conf
```

Optional combined **Sense microphone + IMU + OLED** profile (no external amp required):

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-motion . -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_motion_tuner.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_tuner.conf;configs/xiao_motion.conf"
```

For D2 Back, add `overlays/xiao_third_button.overlay` to the overlay list. The separate [I2S guide](docs/sprint02.md) documents the amplifier profile and safe wiring; do **not** attach the speaker to a GPIO or assume independent overlays can be combined without checking resources. The optional `configs/xiao_quiet.conf` disables diagnostic probes for power measurements.

After a successful build, double-reset the XIAO to mount its **XIAO BLE** UF2 drive and copy the selected `build-*/zephyr/zephyr.uf2` file. Never use a DK image on the XIAO. See [first-boot instructions](docs/xiao-first-boot.md) and the [hardware ledger](docs/hardware-bringup.md).

## Portable checks

```sh
sh tests/host/run.sh
sh tests/platform/run.sh
sh simulator/run-tests.sh
python tools/compile_face_assets.py --check
```

On Windows run `simulator/run.bat`; on macOS/Linux run `./simulator/run.sh`. See [simulator guide](docs/simulator.md). The host suite includes portable checks for the standalone Find Hub experiment. See the [passkey firmware validation](docs/pin-pairing.md) for the current Pico and combined build results.

## Next acceptance milestones

1. Flash a clean chosen build and confirm boot log, OLED, LED, D0/D1 navigation and optional D2 if wired.
2. Validate BLE advertising, the local Pair Phone window, encrypted commands/status and reconnection.
3. Exercise Focus, editable Settings, timer-alert wake and storage-v5 persistence after the five-second save window.
4. Test PDM tuner, IMU reactions and optional I2S only with the relevant enabled profile/hardware.
5. Measure actual current by display, BLE, motion, tuner and audio state before estimating battery life.
6. Complete amplifier shutdown and battery monitoring hardware; then expand companion-app protocol parity.

Central references: [architecture](docs/architecture.md), [product map](docs/product-map.md), [BLE protocol](docs/ble-protocol.md), [OLED power](docs/oled-power.md), [local settings](docs/local-settings.md). [Sprint 01](docs/sprint01.md) and [Sprint 02](docs/sprint02.md) are **historical records**. The [Flutter app](https://github.com/BNextDigital/pico-companion) currently implements only original BLE commands `0x01`–`0x06`, not every firmware-local feature.

### Optional combined Find Hub measurement build

Use [the combined profile guide](docs/combined-find-hub.md) for the opt-in build helper,
actual flash/RAM comparisons, guarded partition/UF2 verification, runtime profiling and
rollback. This milestone links the SDK network code while keeping activation disabled;
ordinary Pico builds are unchanged. No Google credentials are embedded or needed.

### Clock, alarms and stopwatch

Version 0.14.0 adds [Clock](docs/clock.md) to the root icon menu: local time/date,
12/24-hour format, optional face glance/USB desk view, four persisted alarm slots,
manual time/alarm editors and a monotonic stopwatch. New protocol commands support
phone time sync and alarm configuration; the production companion app still needs
to adopt them. See the guide for packet layouts, simulator usage and physical checks.
