# Motion-aware personality

Firmware `0.6.0-motion`, branch `feature/motion-personality`, based on main
`d22830c` after the Music tuner merge. Adds optional accelerometer-based
personality reactions. No extra wiring is needed on the XIAO nRF52840 Sense.
This revision has host/simulator coverage; target build and physical motion
classification have not been validated.

## What Pico does

After roughly two seconds at rest, motion detection arms:

- **Pickup-like movement:** a sustained change from the resting gravity vector
  wakes a sleepy face into a short Curious expression, returning to Neutral.
- **Deliberate rock:** tilt to one side, hold briefly, tilt to the opposite side,
  hold briefly, then return to the starting orientation. Pico reacts Happy for
  three seconds. Use a slow rock, not a sharp shake or impact.
- **Stillness:** after about a minute of both stillness and user inactivity,
  Pico becomes Sleepy. Pickup, gesture or button interaction can wake him.
  Disabling motion retains the existing five-minute inactivity fallback.

Reactions apply only on **Companion/Home, undocked**, with no active/paused timer,
pending completion, focus-phase confirmation, tuner or metronome. Menu and
utility screens retain control. Happy/Alert expressions are not replaced by
motion; completion alerts take priority. Motion never changes modes, starts,
cancels or advances a timer, and never increments the pat counter.

All motion reactions are silent and do not write to flash. A small outlined
square beside the BLE indicator shows configured motion support; its center
fills when the classifier considers Pico settled.

“Wake” here means waking Pico's personality/face from Sleepy. MCU deep sleep,
display power-off and interrupt-driven wakeup are not implemented. An
accelerometer cannot prove an object was picked up or detect constant velocity.
Slow straight-line pickups may be missed, and bag movement can cause a false
wake. Physical thresholds and false-trigger behavior require testing.

## Build and flash

From `pico_firmware` in the nRF Connect SDK 3.4.2 terminal, paste each command as
one line. Keep the prior known-good UF2 for rollback.

Combined **motion + microphone tuner + OLED** profile:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-motion -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_motion_tuner.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_tuner.conf;configs/xiao_motion.conf"
```

Motion/OLED only, for isolating IMU bring-up:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-motion-only -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_motion.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_motion.conf"
```

Double-reset XIAO and copy the selected directory's **zephyr\zephyr.uf2** to its
UF2 drive. No mass erase or bond clearing is required. Overlays include the
OLED layout; do not include multiple overlays that duplicate that node.
Existing OLED-only, tuner-only and audio-only builds remain available, but only
a profile with `xiao_motion.conf` and the IMU alias enables hardware motion.
The combined build enables tuner capture support, not the optional speaker.

## Sensor integration

The SDK Sense board defines an LSM6DS3TR-C through the existing `st,lsm6dsl`
driver at address 0x6A on **i2c0**. The OLED remains on **i2c1**. Its board-defined
power regulator enables P1.08 with a startup delay. The profile enables the SDK
regulator and sensor drivers rather than directly manipulating sensor registers
or bypassing chip-ID checks.

The accelerometer uses a ±4 g range and 26 Hz ODR, read at most once every 40 ms
in the existing main loop. Gyroscope ODR stays zero. No extra worker or faster
main-loop wakeup is added. Driver `sensor_value` acceleration is converted from
m/s² into integer milli-g for the portable classifier.

Setting motion flags to zero sets accelerometer ODR to zero and resets the
classifier; re-enabling requires fresh stillness. This does not shut down the
IMU power rail or establish a low-power/battery budget. Three consecutive read
errors disable motion until restart. A missing/unready sensor leaves buttons,
Companion and utilities operational. On failure, inspect serial logs; do not
force an unsupported chip through a register-level workaround.

## Detection rules

`motion::Detector` has no platform APIs, heap, floating-point math or RNG. It
consumes `{x,y,z}` milli-g plus a monotonic 32-bit millisecond clock:

- Plausible gravity magnitude: 0.85–1.15 g. Impacts/freefall invalidate arming.
- Settled: two seconds with consecutive vector differences <=35 mg (L1) and
  <=80 mg drift from the quiet anchor. These are heuristics, not measured motion.
- Pickup: >=180 mg L1 change sustained for at least 120 ms after arming;
  five-second cooldown, requiring stillness to re-arm.
- Rock: an eligible axis (resting component <=600 mg) changes by >=350 mg in
  each direction, with >=160 ms dwell on each side and >=160 ms back near the
  original vector (L1 distance <=160 mg), all within 2.5 seconds of movement.
- Rock cooldown: ten seconds, plus fresh stillness. The eligible axis is chosen
  from the initial change; fixed flat mounting is not assumed.
- Sample gaps >250 ms discard the sequence instead of inventing quiet time.
- Rest request after sixty seconds of observed stillness, then at most every
  ten seconds. Core separately requires sixty seconds since user activity.
  Already-Sleepy requests have no render/notification effects.

Cooldowns survive impact/gap resets; explicit config changes or adapter restart
reset tracking. Thresholds are currently source constants. Configuration enables
or disables each behavior; threshold calibration UI is not implemented.

Classifier signals enter the existing event queue with timestamps. Core rejects
stale reaction events (>500 ms), applies context/priority rules and asks the
shared Personality engine for temporary expressions or rest. Sleepy no longer
gets overwritten by an autonomous idle expression before activity wakes it.
Per-sample sensor data never goes to BLE or flash. Notifications occur on
accepted reactions and settled/active transitions, not on each sensor sample.

## Configuration and protocol compatibility

Runtime BLE command **`40 ff`** sets a three-bit mask:

| Bit | Value | Behavior |
| --- | --- | --- |
| 0 | 1 | Pickup wake / Curious |
| 1 | 2 | Rock / Happy |
| 2 | 4 | Stillness / Sleepy |

Examples: `40 07` all (default), `40 01` pickup only, `40 03` pickup + rock,
`40 00` off. Values above 7 and wrong packet lengths are rejected. Settings are
volatile and reset to all enabled on reboot; persistence v3 remains unchanged.
Local settings navigation is a later milestone; configuration currently uses
BLE or simulator controls.

The unchanged 16-byte v1 status uses flag bit 7 for **sensor ready + nonzero
motion mask**, and capability bit 7 for motion availability. It does not imply
a reaction is eligible in the current screen/dock context, and does not encode
the mask, stillness or raw samples. All previous IDs/UUIDs/mode IDs/flags stay
stable; clients must mask flags rather than compare the full flag byte.

## Simulator

New controls: Set down, Pick up, Rock, Leave still, and Motion reactions toggle.
They replay raw acceleration samples through the production classifier, not
just inject a mood event. Scenario buttons reset classifier history for a
repeatable demonstration and advance simulated time. JSON exposes readiness,
settled state and configuration.

Backend examples:

```text
motion_test still
motion_test pickup
motion_test rock
motion_test rest
motion_test bump
motion 0 0 1000 40
motion 500 0 866 40
```

`motion X_MG Y_MG Z_MG DELTA_MS` advances simulated time and injects one sample.
Repeat with <=250 ms gaps for continuous motion. Advancing time alone does not
invent sensor samples. Use `ble 40 00` to disable and `event MotionReady 0` to
simulate unavailable hardware. Virtual readiness is restored on power cycle.

## Validation and next physical checks

Passed locally: full C++17 host suite with warnings as errors; all **19 simulator
tests**; ASan/UBSan host suite (LeakSanitizer disabled in this runner); face asset
regeneration, Python syntax and whitespace checks.

New tests cover stillness, pickup persistence, gesture sequence, vertical
mounting, cooldowns/re-arming, impact/fast-motion rejection, sample gaps,
clock rollover, extreme inputs, sleep/wake, priority, unavailable sensor,
config/codec compatibility, and unchanged timer/count/storage behavior.
Previous Music, tuner and focus tests remain passing.

**Unverified:** target compilation, sensor identity/power/ODR, real motion
thresholds, walking/bag/car false triggers, coexistence with live microphone,
BLE/OLED timing, actual current and on-device GUI readability. SDK source review
and synthetic traces do not establish those results. No nRF SDK/cross compiler,
attached board or live simulator GUI is available in this environment.

On the board:

1. Build/flash; confirm `Sense motion ready` in serial. Verify normal buttons,
   menu, face themes and BLE bonding still work.
2. Leave Pico still for two seconds; watch the settled indicator. Gently tilt
   or lift him: expect a brief Curious reaction. Repeat at different orientations.
3. Settle again, then slowly rock left -> right -> center, briefly dwelling at
   each point. Expect Happy once, then Neutral. Repeat within the cooldown:
   no repeated Happy should occur.
4. Leave Companion idle/still for about a minute. Expect Sleepy, then confirm
   motion/button wake. Test recent button activity near the rest threshold.
5. In Music/tuner/menu, during a timer and on completion Alert, repeat the
   movement: screens, timer states and alerts must retain control.
6. Test pickup-only and disabled masks. Disabled motion should stop sensor reads;
   reboot restores defaults without changing durable theme/work duration.
7. Walk with Pico/bag, set him down, move around a car and reconnect BLE. Record
   accidental reactions before enabling motion gestures for any utility action.
   Do not shake a loose breadboard/jumper assembly sharply.

Bring-up diagnostics print `Motion signals=0x.. mg=x,y,z` on transitions:
Settled 01, Active 02, Pickup 04, Rock 08, Rest 10 (bits can combine). These are
classifier decisions, not proof of correct physical interpretation. The optional
quiet profile suppresses diagnostics. Known USB diagnostics are unchanged.

SDK references reviewed at `ncs-v3.4.2`:
[Sense board](https://github.com/nrfconnect/sdk-zephyr/blob/ncs-v3.4.2/boards/seeed/xiao_ble/xiao_ble_nrf52840_sense.dts),
[LSM6DSL driver](https://github.com/nrfconnect/sdk-zephyr/blob/ncs-v3.4.2/drivers/sensor/st/lsm6dsl/lsm6dsl.c),
[driver Kconfig](https://github.com/nrfconnect/sdk-zephyr/blob/ncs-v3.4.2/drivers/sensor/st/lsm6dsl/Kconfig).
