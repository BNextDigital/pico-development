# Pico desktop hardware simulator

## Purpose

The simulator is a permanent host platform for Pico firmware, not a second
implementation of Pico's behavior. Its native backend compiles the production
files in `lib/pico_core/src` and exposes them to a small Tk desktop shell.

The shared portable layer now owns:

- the `Core` event/state/effect machine;
- feature descriptors and mode order;
- BLE command decoding and status encoding;
- the monochrome display compositor; and
- the 16-byte persisted-state codec.

The simulator owns only host representations of hardware: windows, buttons,
an enlarged OLED, a status LED, an audible chirp, a flash file, and a virtual
BLE control panel.

## Run it

### Windows

Double-click `simulator/run.bat`, or from PowerShell:

```powershell
py -3 simulator/pico_sim.py
```

The first launch needs a desktop C++ compiler. Visual Studio Build Tools with
the **Desktop development with C++** workload is the most direct Windows route.
Python must include Tk support; normal python.org Windows installations do.

### macOS or Linux

```sh
./simulator/run.sh
```

The first launch needs `clang++` or `g++` and Python 3 with Tk.

The launcher rebuilds the native backend when any core `.cpp` or `.hpp` is
newer than the executable. Set `PICO_SIM_CXX` if a specific host compiler
should be used.

## What can be exercised

- Primary and secondary physical buttons, including firmware mood/mode rules.
- Home, car, and absent dock contacts.
- Battery readings, including the firmware's unknown value.
- BLE connection state and arbitrary command-characteristic packets.
- Timers at 1x, 10x, or 60x speed.
- The exact portable 128x64 monochrome framebuffer.
- LED heartbeat/connected behavior and 2.4 kHz chirp requests.
- Delayed flash writes, power loss before a save, reboot restoration, and
  factory reset.
- Raw numeric event injection for early feature development.

Keyboard shortcuts are Space for the primary button and M for mode cycling.

## Maintenance contract

Most firmware iterations require no simulator changes:

1. Put product behavior in `lib/pico_core`.
2. Add portable `.cpp` files anywhere under `lib/pico_core/src`; the simulator
   build discovers them automatically.
3. Put OLED composition changes in `display_model.cpp`, which both the Zephyr
   display adapter and simulator call.
4. Put wire-format changes in `protocol.cpp` and persistent-format changes in
   `persistence.cpp` so both targets exercise the same codec.
5. Reopen the simulator. It rebuilds its backend when shared code changed.

A simulator UI change is only needed when a new feature requires a genuinely
new kind of virtual hardware control or when `State` gains fields that should
appear in the friendly dashboard. The generic event injector and BLE packet
writer remain available before that specialized control exists.

## Tests

Run both suites before firmware changes are considered healthy:

```sh
./tests/host/run.sh
./simulator/run-tests.sh
```

The simulator tests launch the real native backend and verify interaction,
docking, BLE decoding, timer completion, framebuffer generation, flash delay,
and reboot restoration. They do not require a display server.

## Process protocol

`simulator/backend/main.cpp` uses newline-delimited commands and JSON snapshots.
This boundary is intentionally simple enough that another UI, automated test
runner, or future game-engine front end can drive the same executable.

Supported commands include:

```text
event PrimaryPressed 0
event DockChanged 2
advance 30
ble 04 1e 00 00 00
status
power_cycle
factory_reset
quit
```

## Sprint 01 controls

The GUI adds theme selection, pause/resume, virtual D0 tap/hold, previous,
select and back. M opens/advances menus; Space remains Interact. The real
framebuffer now includes the timer utility and unavailable-feature screen.

Clock samples are 100 ms (`advance_ms` in the backend); `advance SECONDS` remains
available. Use `python simulator/pico_sim.py --seed 42`, or backend `--seed 42`,
for repeatable idle behavior. State JSON now includes theme, screen, cursor,
closed-eye state, paused timer state and configured duration. Backend clock,
BLE decoder, personality, timers and persistence remain the production C++.

Persistence accepts 16-byte v1/v2 and 20-byte v3 records. Boot always starts in
Companion mode and keeps durable theme/count/duration. Desktop compilation
runs in the ignored simulator build directory so MSVC objects do not dirty
source folders. See `sprint01.md` for test results and unverified hardware.

## Sprint 02 controls

Work / break starts a confirmed-phase cycle; Next phase advances only after
completion. Test cue emits a Chirp effect; Mute toggles runtime output. Sound
is represented by effects and `sound_cue`, not desktop audio playback.
Use raw events `SetTimerDuration 10`, `SetBreakDuration 5`, `StartFocusCycle`,
`NextFocusPhase`, `SetMuted 1`, `TestSound 2` or BLE commands 20–24.
JSON adds `focus_phase` (0 none/1 work/2 break), `focus_waiting`,
`completed_work_sessions`, `break_duration_seconds`, `sound_muted`, `sound_cue`.
Advancing time past completion never auto-starts the next phase. Power cycling
clears session state and mute, and restores the existing durable settings.

## Music controls

Metronome starts Music, Stop beat stops playback, Tap tempo samples current
simulated time, and Set BPM applies the numeric input (30–240). Beats per bar
can be set through `event SetMetronomeBeats 3` or BLE `33 03`. The GUI uses
20 ms clock steps during metronome playback. Use backend `advance_ms` to test
exact boundaries; large advances skip stale beats instead of replaying them.
Music JSON fields and test examples are in [music-metronome.md](music-metronome.md).
The preview remains visual; Chirp effects do not play desktop audio.

## Tuner input simulation

Tuner / A4 input / Silence / Stop mic exercise the actual portable pitch detector.
Backend `tone MILLIHZ` generates two-plus analysis frames: `tone 440000` for A4,
`tone 65406` for C2, and `tone 0` for silence. Input is explicit synthesized PCM,
not host microphone acquisition. Start with `event StartTuner` or BLE `35`.
`event MicrophoneReady 0` simulates unavailable capture; reboot restores the
virtual adapter. JSON exposes tuner state, signal, MIDI note, cents and level.
Details and validation limits: [music-tuner.md](music-tuner.md).

## Motion traces

Set down / Pick up / Rock / Leave still replay timestamped acceleration traces
through the production classifier. The Motion reactions toggle controls runtime
mask 7/0. Backend `motion_test still|pickup|rock|rest|bump` offers repeatable
scenarios; `motion X_MG Y_MG Z_MG DELTA_MS` accepts explicit samples. Large sample
gaps clear arming. Virtual time advances alone do not synthesize accelerometer
data. JSON includes motion readiness, settled state and mask. See
[motion-personality.md](motion-personality.md) for examples and limitations.

## Minute duration editor

Open a timer utility, navigate to Duration and select to edit. Navigate moves
between −1 minute / +1 minute / Save / Cancel; Select activates, Back discards.
Focus adds a Break duration row at index 5. Editor screen ID is 7, and JSON
exposes draft seconds, break target and highlighted action. BLE continues to
accept second-resolution durations. See [minute-duration-controls.md](minute-duration-controls.md).

## Local Settings

The Settings shortcut opens the same hub as BLE 15. Use virtual Navigate,
Select and Back for all draft editors; preview is not committed until Save.
Break, mute, volume and motion now survive power cycle after the coalesced save.
Storage v4 reads older records through the production migration path. JSON adds
settings/edit state and volume; see [local-settings.md](local-settings.md).

The root menu uses the same icon carousel as firmware: Navigate cycles the
nine icons and Action selects, with the caption beneath the icon. See
[icon root menu](icon-root-menu.md) for layout and hardware checks.

Snapshots include `next_core_wake_ms` for timer/personality/music deadlines.
This excludes motion polling, tuner drain and platform worker activity; it is
not a power estimate. Explicit time jumps retain their existing behavior. See
[deadline sleep](deadline-sleep.md) for the host scheduling model and checks.

OLED controls are available through the shared Settings menu. JSON includes
saved/preview brightness, timeout and sleep state; the GUI dims lit pixels for
preview and displays black during sleep. These colors do not model measured
OLED luminance. See [OLED power controls](oled-power.md).
