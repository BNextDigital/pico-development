# Sprint 02: sound cues and work/break sessions

Branch: `sprint/02-audio-and-focus`, based on `main` at `c3ac491` after both
hardware bring-up and Sprint 01 were merged. Firmware version: `0.3.0-sprint02`.
The OLED-only build, D0/D1 inputs, phone-initiated bonding and bring-up diagnostics
are preserved. Nothing is automatically merged into main.

## User behavior

The original Home tap still starts a single focus timer; tapping during an
active/paused timer still pats Pico. Hold D0 on Home cancels Focus. D1 opens or
cycles the menu; D0 selects; hold D1 goes back.

For a repeating session: Menu -> Focus Timer -> **WORK/BREAK**. The configured
work duration defaults to 25 minutes; breaks default to five minutes. The DURATION
editor configures work in one-minute steps; the BREAK editor or BLE `22` configures break duration. See
[minute-duration-controls.md](minute-duration-controls.md) for the newer editor. A cycle cannot
replace a running/paused timer: cancel it first.

Each phase counts down using the existing `Timer`. Pause/resume apply to either
phase. At completion Pico shows Alert/DONE for four seconds, then Home displays
**BREAK READY - TAP** or **WORK READY - TAP**. Nothing starts automatically while
you are away. Tap D0 on Home or select **NEXT PHASE** in the Focus utility to
start the next phase. Repeated Next during a phase has no effect.

Cancel works during a running, paused or waiting cycle. Changing to another
mode or entering a car dock ends the cycle. Utility's HOME action and menu Back
navigation only change screens; explicitly selecting Home mode ends the cycle.
Original BLE `04` starts a standalone timer and replaces a cycle. `01` remains
a pat. Completed work count is per-cycle and clears on cancel/restart.

Theme, interaction count and work-duration persistence stay at v3 (20 bytes),
with v1/v2 migration unchanged. Active cycles, counts, break duration and mute
are runtime settings: restart returns to Companion with five-minute breaks and
sound enabled. No extra flash writes occur on phase ticks or transitions.

## Optional MAX98357A wiring

Unplug USB before rewiring. Match the breakout's printed labels; header order
varies. This is for the **MAX98357A I2S** amplifier, not the MAX98357B
left-justified variant. Use the USB-powered breadboard for first tests.

| XIAO nRF52840 Sense | MAX98357A |
| --- | --- |
| USB 5V / VBUS | VIN |
| GND | GND |
| D8 / P1.13 | BCLK |
| D9 / P1.14 | LRC / LRCLK / WS |
| D10 / P1.15 | DIN |
| 3V3 | SD / SD_MODE (enable, left channel) |
| Leave open | GAIN (chip default 9 dB; verify breakout has no override) |

Connect the **8-ohm, 1 W moving-coil speaker across OUT+ and OUT−**. Neither
speaker terminal connects to GND. Keep OLED VCC on 3V3, SDA on D4 and SCL on D5;
D0/D1 buttons remain connected to GND with internal pull-ups. SPI2 is disabled
in the audio overlay because its header signals share the selected pins.

Both I2S channels contain identical mono samples. SD_MODE is tied high only
while amplifier VIN is powered from USB 5V. Battery supply/amp-enable sequencing
requires a separate design; VBUS is not a battery rail.

PCM is signed 16-bit stereo at 31,250 frames/s. The integer triangle oscillator
has short fades and a hard digital peak ceiling of 2048/32768; the default
25% volume scales that ceiling to 512/32768. This is a conservative starting
setting, **not a measured speaker-power limiter**. Keep the default for first
bring-up and check for distortion before changing gain/volume.

Sources: [MAX98357A/B datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf),
[Adafruit amplifier pinout/reference breakout](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts).
A generic breakout may have different pull-ups/jumpers; inspect its labeling.

## Build and flash (PowerShell, SDK 3.4.2 terminal)

From the repository's **pico_firmware** directory, use the nRF Connect SDK
terminal where `west` is available. SDK installation alone does not put west
on every ordinary PowerShell terminal's PATH.

OLED-only regression build (no amplifier needed):

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao -- -DDTC_OVERLAY_FILE=overlays/xiao_ble_ssd1306.overlay -DEXTRA_CONF_FILE=configs/xiao_ble.conf
```

Optional audio build, in its own directory:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-audio -- -DDTC_OVERLAY_FILE=overlays/xiao_ble_audio.overlay '-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_audio.conf'
```

Use only the audio overlay for that build; it includes the OLED overlay itself.
Double-reset XIAO, then copy `build-xiao-audio\zephyr\zephyr.uf2` onto its UF2
drive. Keep the prior known-good OLED UF2 available for rollback. No mass erase
or bond clearing is required. Inspect the first actual CMake/compiler error if
a build fails; the final Ninja failure is only the summary.

Optional quieter profile: append `;configs/xiao_quiet.conf` to the quoted
EXTRA_CONF_FILE list. It disables periodic OLED address probes and lowers SMP
logging. The normal bring-up profile retains diagnostics. USB console and
20 ms button polling remain: this is **not** a low-power/battery build.

## Fast demonstration over encrypted BLE

Bond/connect as before, write hex to the existing command characteristic:

| Bytes | Action |
| --- | --- |
| `23 00` | Short interaction cue |
| `23 01` | Timer-complete cue |
| `23 02` | Work-complete cue |
| `23 03` | Break-complete cue |
| `24 01` / `24 00` | Mute / unmute (runtime only) |
| `0A 0A 00 00 00` | Work duration 10 seconds |
| `22 05 00 00 00` | Break duration 5 seconds |
| `20` | Start work/break cycle |
| `08` / `09` | Pause / resume current phase |
| `21` | Confirm next phase after completion |
| `05` | Cancel cycle, including while waiting |

TestSound does not change the face or increment interaction count. On a build
without audio, visual timer alerts continue. An optional existing PWM buzzer
can provide fallback chirps. A runtime mute clears queued I2S cues; a cue already
playing may finish (at most 300 ms). Audio readiness indicates successful I2S
configuration, not detection of a connected amplifier/speaker.

## Implementation and verification

- `FocusSession` only sequences Work/Break/waiting/count; it owns no clock.
  `Core` routes start/pause/resume/cancel to the same `Timer` used by single timers.
- `audio.cpp` produces deterministic PCM without allocation, floating point,
  shared oscillator state or platform headers; chunk boundaries do not affect
  samples. Four short, distinct scores are compiled into firmware.
- `pico_audio.cpp` owns the Zephyr I2S device, four-block slab and four-cue queue.
  A sleeping worker performs bounded waits; main/BLE only enqueue. DMA takes
  ownership of each successfully queued block. Failed queues free caller-owned
  blocks; a failed stream is dropped/recovered. Completion cues replace a full
  queue of pending cues. Persistent buffer failure disables audio until restart.
- Simulator adds cycle/Next/test-cue/mute controls and session fields in JSON.
  It exposes Chirp effects; it does not play audio through the desktop soundcard.
- BLE IDs/UUIDs/status length remain stable; appended commands and flag bits are
  in [ble-protocol.md](ble-protocol.md). No new persistence format.

Local validation on Linux:

```sh
sh tests/host/run.sh
sh simulator/run-tests.sh
python tools/compile_face_assets.py --check
```

Passed: host suite with `-Wall -Wextra -Wpedantic -Werror`, all 12 simulator tests,
asset regeneration check, and AddressSanitizer/UndefinedBehaviorSanitizer host
run (`ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer unavailable under this runner).
New coverage checks sample bounds/stereo/chunk equivalence/mute/padding, phase
confirmation, single completion, pause/resume, cancellation, mode/dock changes,
menu bounds, legacy timer replacement, status and malformed new commands.

**Not validated here:** Zephyr target compilation, physical I2S clock/data,
speaker output/loudness/pop behavior, DMA recovery under live BLE traffic,
current draw, GUI layout on a live desktop. The SDK/toolchain and attached
board are unavailable in this environment. SDK 3.4.2 I2S headers, nrfx driver
and pinctrl definitions were reviewed against `ncs-v3.4.2`; this is source
review, not a successful target build.

## Physical acceptance checklist

1. Build/flash OLED-only first. Confirm menu, both button holds, eight themes,
   BLE reconnect/bond, and the old one-tap single timer still work.
2. Wire the powered-off amplifier by labels, then build/flash audio profile.
   Confirm `MAX98357 I2S ready` log and trigger `23 00` at default volume.
3. Test all four cues, mute/unmute, repeated pats and BLE commands during audio.
   Check that face/button/serial response remains responsive and no slab errors
   or resets occur. The pre-existing USB `net_buf 4095` warning remains unresolved.
4. Run the ten-second/five-second cycle. Pause each phase, verify no elapsed time
   while paused, completion once, four-second alert, and indefinite READY wait.
5. Confirm next with D0 and then via BLE. Cancel in active/paused/waiting states;
   test menu mode changes and a power cycle. Durables must survive; cycles reset.
6. Disconnect the phone and repeat local controls. Test the OLED-only profile
   with the amplifier unplugged. A configured I2S port cannot detect missing amp.
7. Record idle/advertising/connected/timer/audio current with OLED fixed, same
   supply, same meter and same BLE state. Compare diagnostics on/off. Do not
   infer battery life from USB supply measurements or a software timeout alone.

## Next priorities

1. Complete the target/physical checklist and address any I2S/USB regressions.
2. Replace button polling with GPIO interrupts plus deferred debounce; measure
   sleep/current and add controllable amplifier shutdown before battery work.
3. Refine work/break UI from actual use, expose break duration/mute locally,
   then bring these additive BLE commands into the companion app.
4. Bring up IMU/PDM and select one narrow music utility after input timing is
   measured. Avoid expanding the mode list before those adapters are verified.
