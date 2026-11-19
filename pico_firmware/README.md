# Pico firmware

Offline-first Zephyr/C++17 firmware for Pico, a small BLE companion with expressive 128×64 monochrome face, contextual docks and local tools. Target hardware: **Seeed Studio XIAO nRF52840 Sense** with **nRF Connect SDK 3.4.2**.

**Snapshot (November 18, 2026):** the source build-version default is `0.11.0-music-reference` (`Kconfig` and `VERSION`). This is a software milestone, **not a hardware-validated release**. Earlier XIAO OLED, BLE and D0 behavior were physically demonstrated; compilation and acceptance of the latest combined profile are still pending.

## Implemented software

| Capability | Current implementation / boundary |
| --- | --- |
| Personality | Eight layered bitmap faces, blink/idle behavior and guarded reactions |
| Controls | D0 Action, D1 Navigate, optional D2 Back; icon root menu; GPIO interrupts with deferred debounce/hold |
| Time | Reusable pause/resume timer, Cooking/general timer, Focus work/break confirmation, one-minute local editors |
| Music | Visual metronome, optional Sense PDM chromatic tuner and E2–C6 reference pitches through optional I2S audio |
| Motion | Optional Sense IMU pickup/rock/rest classification and personality behavior mask |
| Audio | Optional MAX98357A I2S sound cues, volume/mute and visual-only fallback |
| Settings | Eleven menu items: theme, mute, volume, work/timer, break, motion, test sound, brightness, screen time, sleep now and back |
| OLED power | 5–100% brightness, Off/1–30-minute inactivity timeout, wake-only first input, timer-alert wake |
| Persistence | 32-byte **storage v5**, migration from v1–v4, coalesced saves; transient timers and editors not restored |
| BLE | Encrypted custom GATT protocol v1, 16-byte status, additive commands; bonds stored separately |

Optional peripherals have hardware adapters and host coverage, but **target compilation, audio/acoustic accuracy, OLED contrast, button interrupts, sensor thresholds and current draw need real-board validation**. Wayfinding GPS/compass, Notes recording, complete dock electronics and final battery/power hardware are not implemented.

## Music reference pitches

Music -> Tone adds note/frequency selection and two-second E2–C6 reference
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

On Windows run `simulator/run.bat`; on macOS/Linux run `./simulator/run.sh`. See [simulator guide](docs/simulator.md). Earlier feature PRs document successful local tests. **These tests and the target build were not rerun during this documentation update.**

## Next acceptance milestones

1. Flash a clean chosen build and confirm boot log, OLED, LED, D0/D1 navigation and optional D2 if wired.
2. Validate BLE advertising, phone-initiated bonding, encrypted commands/status and reconnection.
3. Exercise Focus, editable Settings, timer-alert wake and storage-v5 persistence after the five-second save window.
4. Test PDM tuner, IMU reactions and optional I2S only with the relevant enabled profile/hardware.
5. Measure actual current by display, BLE, motion, tuner and audio state before estimating battery life.
6. Complete amplifier shutdown and battery monitoring hardware; then expand companion-app protocol parity.

Central references: [architecture](docs/architecture.md), [product map](docs/product-map.md), [BLE protocol](docs/ble-protocol.md), [OLED power](docs/oled-power.md), [local settings](docs/local-settings.md). [Sprint 01](docs/sprint01.md) and [Sprint 02](docs/sprint02.md) are **historical records**. The [Flutter app](https://github.com/BNextDigital/pico-companion) currently implements only original BLE commands `0x01`–`0x06`, not every firmware-local feature.
