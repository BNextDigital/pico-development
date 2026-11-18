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

## Build

Open an nRF Connect SDK terminal, then build for your actual board. For the
nRF52840 DK without an external display:

```sh
west build -p always -b nrf52840dk/nrf52840 .
west flash
```

For the common provisional SSD1306 wiring, use the explicit overlay shown in
[`docs/hardware-bringup.md`](docs/hardware-bringup.md).

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
