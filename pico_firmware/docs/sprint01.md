# Sprint 01 — personality and interactive utilities

## Baseline and validation status

Based on `hardware/xiao-oled-bringup` at `13c219096a6b4a4d91fb60b9d2728e2b7896c5fb`,
eight commits ahead of `main` (`bd2678e`). The hardware branch is preserved.
No merge into `main` is part of this sprint delivery.

The baseline passed its portable core suite and seven simulator tests. The
user had physically confirmed OLED rendering, BLE bonding/interaction, and
D0 tap/hold controls on that baseline. Those observations **do not** validate
the new firmware in this branch.

New revision validation:

| Check | Result |
| --- | --- |
| Portable C++17 tests, warnings treated as errors | Pass: 18 test groups |
| Desktop backend/UI-helper tests | Pass: 10 tests |
| AddressSanitizer + UndefinedBehaviorSanitizer | Pass with leak detection disabled |
| LeakSanitizer | Unavailable under this environment's process tracing |
| Packed BMP asset reproducibility | Pass: `compile_face_assets.py --check` |
| Python syntax checks | Pass |
| Whitespace / diff checks | Pass |
| Actual framebuffer review | 40 theme/expression combinations inspected |
| Zephyr / nRF Connect SDK 3.4.2 target build | Not run: toolchain not installed in execution environment |
| Live Tk desktop window | Not exercised: no display server here |
| New physical firmware, optional D1, timing/power measurements | Not yet validated |

## Implemented behavior

- All eight stable theme IDs select real packed primitives from the existing
  BMP pack: Modern, Chibi, Anime, Retro IBM, Mecha, Vintage Cartoon, Minimal
  Emoticon, Kawaii Mascot.
- Neutral, happy, curious, sleepy, and alert expressions; alert adds an
  unmistakable exclamation accent.
- Seeded deterministic blinks every roughly 4–7 seconds, with a 150 ms closed
  interval (quantized by the 100 ms animation sampler).
- Quiet curious glances roughly every 20–40 seconds when idle; sleepy after
  five minutes without local/command activity. Interactions wake Pico.
- Temporary interactions last four seconds; idle glances last two seconds.
  Timer completion has expression/screen priority. Menus suppress idle antics.
- Face/home, scrolling menu, theme picker, timer utility, unavailable-feature
  screen, selection, previous/next, and back navigation.
- A shared timer service handles arbitrary valid durations, start, pause,
  resume, cancel and one-shot completion. It uses supplied monotonic elapsed
  time, not a count of delivered ticks. Focus and Cooking reuse it.
- Countdown text, paused state, completion expression, optional chirp effect,
  then return to the home screen after four seconds.
- Theme and chosen timer duration persist. Every boot begins in Companion
  mode; active/paused timers, menus and transient moods are not resurrected.

## Inputs

The brief's statement that no external buttons were configured was stale.
D0 controls had already been implemented and physically tested. They remain:

| Context | D0 tap/release | D0 hold about 1 second |
| --- | --- | --- |
| Home, no timer | Start Focus using configured duration (25 minutes initially) | No action |
| Home, active/paused timer | Pat/smile without restarting | Cancel a Focus timer and return to Companion |
| Menu | Select highlighted item | Back to Home |
| Timer utility | Activate highlighted action | Back to Menu |
| Theme picker | Accept theme and return Home | Back to Menu |

Optional second normally-open switch: **D1 (P0.03) to GND**. Tap opens the menu
from Home, then advances the cursor. Hold goes back. D0 remains D0 (P0.02) to
GND. Both use internal pull-ups and portable 30 ms debounce / hold detection.
Do not move the working OLED wires: VCC=3V3, GND=GND, SDA=D4, SCL=D5.

No second switch is necessary to try menus through the simulator or new BLE
navigation commands. Remove/change the `pico-nav-button` devicetree alias to
remap or omit that input. The implementation does not assume a touchscreen.

Timer utility actions are Duration, Start/Pause/Resume, Cancel, Home. The local
duration selector cycles 1, 5, 25, 50 minutes. BLE accepts any 1–604800 seconds.
Changing the preset affects the next timer, not a running timer. The timer
utility is one shared timer; simultaneous timers and automatic breaks are
future features.

## Architecture and changed-file map

| Area | Main paths | Decision |
| --- | --- | --- |
| Personality | `lib/pico_core/{include/pico,src}/personality.*` | Caller-controlled xorshift seed, supplied time, no OS/RNG/flash calls |
| Timing | `lib/pico_core/{include/pico,src}/timer.*` | Allocation-free reusable timer instance; future features can own additional instances |
| State/navigation | `lib/pico_core/{include/pico,src}/core.*`, `types.hpp` | Keep stable IDs; central orchestration of services and screens |
| Feature registry | `features.cpp` | Focus and Cooking share the utility; unimplemented features present an explicit unavailable screen |
| Faces | `display_model.cpp`, `face_assets.hpp` | Palette-aware 1-bit primitives, signed/clipped blits, 128x68 anchors center-cropped to 128x64 |
| Asset tool | `tools/compile_face_assets.py` | Standard-library-only deterministic conversion; checked-in generated C++ is source, not a build binary |
| Persistence | `persistence.*`, `src/platform/pico_storage.cpp` | Read versions 1/2/3; write version 3; coalesce durable changes |
| Protocol | `protocol.*`, `docs/ble-protocol.md` | Preserve IDs 01–06 and 16-byte v1 status; append commands and paused flag |
| Board wiring | `overlays/xiao_ble_ssd1306.overlay`, `pico_hardware.cpp` | Optional D1 navigation; no changes to proven D0/OLED pins |
| Board orchestration | `src/main.cpp`, `CMakeLists.txt` | Add 100 ms animation sampling, compile shared service sources |
| OLED adapter | `src/platform/pico_display.cpp` | Skip identical frames; only cache after successful transfer |
| Simulator | `simulator/backend/main.cpp`, `pico_sim.py`, `build_backend.py` | Subsecond time, seeds, new controls/state, cached previews; build objects remain in ignored build directory |
| Regression tests | `tests/host/sprint01_tests.hpp`, `core_tests.cpp`, `simulator/tests/test_backend.py` | Services, animation, all faces/layouts, menu flow, persistence migrations, legacy packets |

The descriptor registry remains intentionally small. Actual reusable timing
and personality logic lives in modules, rather than duplicating it per mode.
The app supplies commands/data, never arbitrary executable C++ modules.

## Compatibility and persistence

Mode IDs and all original event IDs/command IDs retain their values. The status
packet remains 16 bytes with protocol byte 1; unused flags bit 2 now means
paused. BLE `01` remains Interact, not a timer start. New commands are documented
in `ble-protocol.md`.

| Record version | Size | Migration |
| --- | --- | --- |
| 1 | 16 bytes | Preserve count; default theme Modern and duration 1500 s |
| 2 | 16 bytes | Preserve count/theme; default duration 1500 s |
| 3 | 20 bytes | Also preserve configured duration; checksum covers new fields |

The new reader rejects unsupported versions, wrong lengths, checksum errors,
invalid enums and invalid durations without partially modifying output state.
Legacy records are upgraded on the next durable save, not on every boot.
BLE bonds use their own settings keys and are not erased.

Downgrading to the original hardware branch after a v3 save causes its older
reader to ignore the 20-byte Pico state and use defaults. It cannot preserve
those newly saved settings across the downgrade. This does not erase BLE bonds.

## Frame and power policy

Core animation changes request Render only when visual state changes. Blink
and idle transitions never request persistence. Blink-only changes do not
notify BLE. A host regression samples 3000 animation updates and checks fewer
than 200 visual changes over five idle minutes. The platform also skips an
I2C transfer when the composed framebuffer is unchanged.

The existing 20 ms physical-button polling remains; the 100 ms animation
sampling runs from the same loop, not another thread. This is not a measured
battery-life claim. Interrupt-driven inputs, display sleep and advertising
policy still require an instrumented power pass. Bring-up diagnostics and SMP
debug configuration are preserved pending physical validation. Repeating
`frame_written=1` now also includes a successfully retained, unchanged frame.

## Flash and validate on XIAO

In an nRF Connect SDK **3.4.2 terminal**, from your local repository:

```powershell
git fetch origin
git switch sprint/01-personality-and-utilities
cd pico_firmware
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-sprint01 . -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_ssd1306.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf"
```

If already in `pico_firmware`, omit `cd pico_firmware`. Build into a separate
`build-sprint01` directory to keep the old build available. After a successful
build, double-tap reset and copy `build-sprint01/zephyr/zephyr.uf2` to XIAO BLE.
Do not erase the bootloader or flash the DK image. The working hardware branch
remains available as a rollback reference.

Physical acceptance sequence:

1. Face/heartbeat and BLE advertisement; existing diagnostic output still works.
2. Confirm a blink while idle. Wait five minutes for sleepy, tap to wake/start.
3. Verify the known D0 quick-start and hold-to-cancel behavior again.
4. Try all eight themes through `07 00` … `07 07`; verify the OLED geometry.
5. If D1 is fitted, tap to open/cycle menu, D0 to select, hold for back.
6. Open Focus utility via `10`, `13`; configure 10 s using `0A 0A 00 00 00`,
   select Start (`13`), pause (`08`), wait, resume (`09`). Check paused countdown
   stays fixed, completion appears once, then returns Home after four seconds.
7. Confirm a BLE reconnect/bond still works. Existing phone-initiated pairing
   configuration and encrypted permissions remain unchanged.
8. Change theme/preset, allow at least five seconds for coalesced save, reboot,
   and confirm Companion/Home with the selected theme/preset.
9. Test through a disconnected phone. Missing audio/docks/microphone features
   must not prevent the face/timer from operating.

The completion chirp is an Effect and uses an optional existing PWM adapter.
The actual MAX98357 I2S amplifier is **not** driven by this sprint. A visual
completion alert is expected on the current board. The pre-existing USB
`net_buf 4095` diagnostic also remains unresolved; serial data and BLE must be
checked independently.

## Local verification commands

```sh
sh tests/host/run.sh
sh simulator/run-tests.sh
python tools/compile_face_assets.py --check
python simulator/pico_sim.py --seed 42
```

The simulator GUI now has theme, pause/resume, D0 tap/hold, menu selection and
back controls. Keyboard Space pats; M opens/advances the menu. Its clock runs
in 100 ms steps, including time acceleration. The backend also accepts
`advance_ms 100` and `--seed 42` for deterministic behavioral replay.

## Sprint 02 priorities

1. Finish the target build and physical Sprint 01 acceptance sequence; fix any
   OLED/input/USB regressions before expanding features.
2. Implement the MAX98357 I2S adapter, with conservative digital volume for
   the 8-ohm/1 W speaker, and distinct completion/interact sounds.
3. Replace polling with interrupt/deferred button handling and measure idle,
   advertising and OLED power before adding a battery/sleep policy.
4. Extend the same timer service into explicit work/break sessions and improve
   menu labels/accessibility based on use on the actual small display.
5. Bring up onboard IMU/PDM input; choose one narrow next utility (metronome
   first, tuner after microphone capture is measured). Docks/phone app/peers
   follow stable local behavior rather than expanding the mode list now.
