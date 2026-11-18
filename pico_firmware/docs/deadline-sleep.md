# Deadline-based main-loop waits

The main loop now waits until the earliest required service deadline instead
of waking on a fixed 20 ms timeout. GPIO button and BLE events still wake the
message queue early. Zephyr can idle between events; this does not enter System
OFF or disconnect BLE. No wiring, overlay, protocol or persistence change.

| Service | Next required work |
| --- | --- |
| Timer, status LED, diagnostics | Next absolute uptime second |
| Personality | Blink close/open, temporary expression expiry, idle reaction or inactivity threshold |
| Metronome | Next beat or the end of its 80 ms visual pulse |
| Enabled, ready motion adapter | Remaining time to its 40 ms sample deadline |
| Active tuner | At most 20 ms until the next main-loop microphone drain |
| Buttons / BLE | Event queue wakes early; button debounce uses separate deferred work |

Personality deadlines ignore actions that are blocked by the current screen,
temporary expression or sleepy state. In particular, a closed eye waits for
its opening deadline rather than repeatedly considering the consumed blink
start. Music uses its existing rational phase so non-integer periods (such as
137 BPM) retain their timing and delayed callers still avoid catch-up bursts.
Inactive music, missing/disabled motion and inactive tuner add no service wake.

The portable `schedule::wait_ms` selects the minimum relative delay. It uses
64-bit uptime for second boundaries and wrap-safe relative millisecond clocks
for personality/music/motion. Any work that is already due requests a zero
wait, allowing another service pass after a slow display or diagnostic call.
Timeouts are calculated using fresh uptime after work, not before it.

The loop refreshes second, music and animation clocks before dispatching an
external event. A pause therefore accounts for elapsed whole seconds and a
reaction/tap uses the wake timestamp rather than the last idle sample. Tick
and animation effects remain authoritative for timer completion and return home.
Display rendering stays effect-driven; this adds no periodic full-frame update.

## Simulator and tests

Simulator snapshots expose `next_core_wake_ms`: timer/personality/music only,
excluding physical motion polling and microphone-drain limits. Existing
`advance`/`advance_ms` remain explicit time jumps; they do not run intervening
wakeups or model radio/CPU power. The host test drives the same deadline APIs
through all intervening service passes and compares every visible transition
and timestamp against a 1 ms reference for idle personality, 137 BPM music,
and timer completion/alert return. It also asserts a bound on service-pass count.

Local results: host/core and GPIO adapter tests pass, all 25 simulator tests
pass, and the quiet 301-second model runs 421 service passes versus roughly
15,050 fixed-20-ms passes. That model has motion and tuner off and no external
events; it is not a hardware current measurement. The host core also passes
ASan/UBSan with leak detection disabled; packed face assets match their sources.

Additional tests cover due/overdue work, tuner/motion limits, busy/sleepy state,
fractional beats and pulse ends, 32-bit millisecond wrap, and expression expiry.
Run `bash tests/host/run.sh` and `bash simulator/run-tests.sh`.

No Nordic SDK or attached device is available in the implementation environment.
Host/model tests do not establish a successful target build or physical timing.
Main-loop service counts are not whole-device wake counts or battery savings:
BLE, USB, button work, microphone/audio workers and other kernel activity are
outside that model. An enabled IMU still needs periodic sampling.

## Build and physical acceptance

This branch includes the interrupt-button foundation. Use the existing XIAO
SDK 3.4.2 profile without changing wiring. From `pico_firmware`:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-motion -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_motion_tuner.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_tuner.conf;configs/xiao_motion.conf"
```

Keep existing optional audio/third-button overlay additions if used. Copy
`build-xiao-motion/zephyr/zephyr.uf2` using the established bootloader procedure;
no erase or bond clearing. Bring-up diagnostics remain enabled if configured.

1. Leave Pico idle with motion disabled, then enabled. Check natural blinking,
   temporary reactions, inactivity sleep and immediate button/BLE wake.
2. Check all tap/hold/Back paths, including rapidly alternating buttons.
3. Run short timers: pause just before expiry, resume, cancel and complete while
   editing Settings. Confirm one alert and normal return from completion.
4. Run metronome at 30, 137 and 240 BPM with BLE/button traffic; check cadence,
   80 ms visual pulses, sound (if wired), tap tempo and stopping.
5. Start/stop/re-enter tuner repeatedly and confirm responsive readings. Check
   IMU pickup/rock behavior with motion enabled and after toggling its setting.
6. Repeat with diagnostics off. Measure idle/connected/music/tuner/motion current
   under the same supply and peripheral configuration before judging savings.

Next: measured peripheral idle policy, confirmed amplifier shutdown wiring and
OLED contrast/display-sleep controls with explicit wake/alert priority.
