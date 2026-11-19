# Pocket UI and quieter status LED

Source version: `0.12.0-pocket-ui`. This revision targets the actual 128×64
monochrome display. Host/simulator verification does not prove OLED readability,
Nordic compilation or physical LED behavior.

## Display

Apps and Settings now show one selected action at a time instead of tiny grids
or multi-row control lists. Navigate cycles actions; Action activates the one
shown. Labels normally use 10-pixel-high glyphs; the primary value uses up to
20-pixel-high glyphs. Longer strings reduce size to fit. The icon root menu
remains unchanged. The portable renderer retains its compact fallback for
surfaces down to 64×32 and both framebuffer packing formats.

| Screen | Main information | Selected control |
| --- | --- | --- |
| Music | Large BPM, beat number/bar count, brief pulse underline | BPM −/+, Start/Stop, Tap Tempo, Beats +, Tuner, Reference, Home |
| Tuner | Large note, sharp/flat cents or short signal prompt | Listen/Stop |
| Reference | Large note and frequency | Note −/+, Play/Stop, Octave −/+, Back |
| Focus/Cooking | Countdown or selected duration/goal, short progress context | Existing utility actions, one at a time |
| Settings | Setting name and current value | Change, Play, Sleep or Back |
| Editors | Large draft value or face preview | Decrease/Increase (duration: −1/+1 minute), Save, Cancel |
| History | Completed work duration, round count, THIS BOOT label | Back; Navigate browses records |

The face's two upper-left dots were BLE connection and motion-availability
indicators. They and the dock marker are removed from the face; their state
remains available in diagnostics/simulator JSON. Active timer/phase status and
completion alerts remain visible in a larger footer. Theme assets are retained.

## Buttons

| Input | Home/face | Apps/menu |
| --- | --- | --- |
| Action / D0 tap | Pet only; temporary happy reaction | Activate selected action |
| Navigate / D1 tap | Open root menu | Next action/item |
| Hold either button | Back; no timer start/cancel/phase advance | Back/discard draft |
| Optional D2 | Back | Back/discard draft |

To start a timer/session or advance a waiting Work/Break phase, open Focus and
select Start Timer, Start Cycle or Next Phase. Cancel is explicit in the
utility. Home petting preserves running/paused timers and waiting phases.
The first button while the display is asleep still wakes only. Existing BLE
commands, mode IDs, event IDs and storage-v5 bytes are unchanged. Legacy event
names `FocusButtonTapped/Held` remain for compatibility with simulator clients.

## Heartbeat

The XIAO RGB status LED uses green (`led1`, active-low P0.30): 200 ms on,
5000 ms off, repeating every 5200 ms. Connection no longer forces it solid.
The overlay provides explicit status/red/blue aliases; red/blue start inactive.
The separate battery-charge indicator is outside this heartbeat policy.
Other boards retain their `led0` fallback when no status alias is provided.

A dedicated delayable worker schedules only the two pulse edges per cycle;
it does not add polling to the deadline-driven application loop. GPIO failure
stops the worker with a warning. Workqueue latency can delay a physical edge.
The simulator shares timing constants and tests ideal phase boundaries.

## Verification and flashing

Run from `pico_firmware`:

```sh
bash tests/host/run.sh
bash simulator/run-tests.sh
python tools/compile_face_assets.py --check
```

Coverage includes Home pet-only behavior while idle/running/waiting, explicit
phase navigation, face pixels independent of status flags, framebuffer layouts
and bounds, GPIO heartbeat deadlines/failure, and connection-independent LED
phase. Existing host/protocol/persistence regressions remain enabled.

Use the [README build profile](../README.md#build-and-flash) matching your
hardware, preserving optional audio/third-button overlays. Keep `-p always`:
the green LED aliases require devicetree regeneration. Flash that build's
`zephyr/zephyr.uf2` using the existing double-reset method. No erase or bond
reset is required.

Physical acceptance remains outstanding:

1. Check green 200 ms pulses separated by five dark seconds, both advertising
   and connected. Confirm red/blue status channels stay off.
2. Check clean face corners and pet-only Action; hold on Home must not start,
   cancel or advance a timer. Repeat while a phase waits and a timer runs.
3. Browse every Music/Focus/Settings action on the actual OLED. Check long
   labels, decimal frequencies, accidentals, 1/99-minute durations and history.
4. Test Save/Cancel, wake-only first button, completion alert priority and BLE
   commands. Reboot to verify durable settings after the save delay.

The SDK/toolchain and attached board are unavailable here. No target-build,
physical readability, pulse measurement or power-consumption result is claimed.
