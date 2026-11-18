# Pico fundamental firmware prototype

This is the first coherent firmware skeleton for Pico: the small, offline-first
BLE companion with a monochrome face, physical personality, contextual docks,
and focused utility modes.

Version `0.1.0-prototype` already provides:

- a deterministic, host-tested C++17 personality/mode core;
- companion interactions and a deadline-based focus/general timer;
- car-dock entry into driving mode with safe restoration on undock;
- layered monochrome face components for a 128x64 OLED;
- physical buttons, status LED, optional dock inputs, and optional PWM chirps;
- an encrypted custom BLE GATT service for commands and status notifications;
- NVS-backed personality state and BLE bonds; and
- compile-time feature descriptors for companion, focus, music, cooking,
  wayfinding, notes, and driving modes.

The project intentionally does not invent pin assignments, a microphone
driver, battery ADC math, or GPS/compass hardware before those exact parts are
known. Every uncertain peripheral is isolated behind devicetree and a platform
adapter, so the portable core does not change when the breadboard wiring does.

## Repository map

| Path | Role |
| --- | --- |
| `lib/pico_core` | Hardware-independent state, feature registry, and BLE codec |
| `src/platform` | Zephyr adapters for BLE, OLED, GPIO/PWM, and settings |
| `src/main.cpp` | Event-loop orchestration only |
| `simulator` | Reusable desktop hardware simulator using the production core |
| `overlays` | Explicit, replaceable prototype wiring |
| `tests/host` | Fast tests that run without a Nordic SDK or board |
| `docs` | Architecture, protocol, and safe hardware bring-up |

The decisions distilled from the wider Pico project are recorded in
[`docs/product-map.md`](docs/product-map.md); the confirmed-versus-provisional
hardware ledger is in
[`docs/hardware-bringup.md`](docs/hardware-bringup.md).

## Icon root menu

The root menu now uses a large icon with a caption underneath and position dots.
Navigate cycles icons and Action selects; button mappings remain unchanged. See
[icon menu layout and validation](docs/icon-root-menu.md).

## Local Settings

Menu -> Settings adds theme preview, mute/volume, work/break duration, motion
preferences and test sound with Save/Cancel. Storage v4 migrates older records
and preserves all confirmed preferences. Existing two-button controls remain;
a third Back button is optional. See [Local Settings](docs/local-settings.md).

## Minute duration controls

Duration presets are replaced by a ±1-minute preview editor with Save/Cancel,
including a local Break row in Focus. See
[minute duration controls and validation](docs/minute-duration-controls.md).

## Motion personality

The optional Sense IMU profile adds pickup-like wake, a deliberate rock/Happy
reaction and inactivity-plus-stillness Sleepy behavior. Motion stays out of
utility/menu/timer controls and does not write flash. See
[motion build, controls and physical checks](docs/motion-personality.md).
Target/physical validation is still pending; host/simulator checks pass.

## Onboard microphone tuner

The optional Sense microphone profile adds a local chromatic tuner with note,
cents, sharp/flat meter and signal-level guidance. No extra wiring or speaker.
See [tuner build and physical validation](docs/music-tuner.md). Target capture
and acoustic accuracy remain unverified; host/simulator tests pass.

## Music metronome

The Music menu now opens an offline visual metronome with 30–240 BPM,
1–8 beats per bar, tap tempo and optional accented I2S clicks. See
[Music controls, build and validation](docs/music-metronome.md).
Target compilation and physical timing checks remain outstanding.

## Sprint 02

Optional MAX98357A I2S sound cues and confirmed work/break cycles are implemented
on `sprint/02-audio-and-focus`. The OLED-only build remains supported.
See [Sprint 02 build, wiring and validation](docs/sprint02.md). Host/simulator
checks pass; target compilation and physical audio testing remain outstanding.

## Sprint 01

The dedicated `sprint/01-personality-and-utilities` branch adds eight rendered
face themes, deterministic blinks/idle behavior, navigation and a reusable
pause/resume timer. Start with [the sprint delivery and XIAO validation guide](docs/sprint01.md).
The existing D0 focus controls remain available; optional D1 adds menu navigation.
Target build and new physical validation are pending on the local SDK/board.

## Build

For the purchased XIAO nRF52840 Sense and SDK 3.4.2, open an **nRF Connect
SDK terminal** in `pico_firmware` and build the explicit XIAO overlay:

```sh
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-sprint01 . -- -DDTC_OVERLAY_FILE=overlays/xiao_ble_ssd1306.overlay -DEXTRA_CONF_FILE=configs/xiao_ble.conf
```

Copy `build-sprint01/zephyr/zephyr.uf2` to the double-reset **XIAO BLE** drive.
See [Sprint 01](docs/sprint01.md) for new-input wiring and physical validation.
The older DK overlay is a separate example; do not flash DK output to the XIAO.

Run the portable core tests with any C++17 compiler:

```sh
./tests/host/run.sh
```

## Desktop simulator

The desktop simulator compiles and runs the exact portable firmware core, BLE
codec, display compositor, and persistence codec used by the board build. Its
host shell supplies virtual buttons, docks, battery input, BLE packets, OLED,
LED, speaker chirps, flash storage, and accelerated time.

On Windows, double-click `simulator/run.bat`. On macOS or Linux, run:

```sh
./simulator/run.sh
```

The backend rebuilds itself when shared core sources or headers change, so
ordinary firmware behavior and sprite iterations do not require a new
simulator. See [`docs/simulator.md`](docs/simulator.md) for setup, controls,
extension rules, and headless tests.

The code targets current Zephyr-style board names and APIs in nRF Connect SDK
3.x. Older releases may require the legacy underscore board name documented in
the bring-up guide and small API compatibility adjustments.

## First milestone

The breadboard milestone is deliberately small and observable:

1. Pico boots and advertises.
2. A button makes Pico react locally without a phone.
3. A bonded phone reads status and triggers the same interaction.
4. The OLED face and LED reflect state.
5. A 30-second timer completes with a visible alert and, once safely wired, a
   short chirp.
6. Reset preserves the selected non-driving mode and interaction count.

Once that passes on the purchased board, the next work item is an exact board
overlay plus microphone/battery bring-up—not a rewrite of the core.
