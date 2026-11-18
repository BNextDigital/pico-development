# One-minute duration controls

> **Current-state note (November 18, 2026):** This editor remains implemented. Local Settings subsequently made break duration durable in storage v4, and OLED power extended that format to **v5**. The introduction-era branch/format claims below are historical.


The subsequent [Local Settings buildout](local-settings.md) makes break duration
durable and adds the Settings hub. Its v4 storage details supersede the original
runtime-only break notes below.

Firmware `0.6.1-minute-controls`, branch `feature/minute-duration-controls`, based
on main `983eb56` after motion personality merged.

The on-device duration presets (1, 5, 25 and 50 minutes) are replaced by a
reusable minute editor. Work/standalone timer duration and break duration use
**exactly 60-second steps**, with explicit Save and Cancel.

## Controls

Open Focus or Cooking and select **DURATION**. Focus additionally has **BREAK**
as its last row, after WORK/BREAK. This opens the break-duration editor.

| Action | Behavior |
| --- | --- |
| −1 MIN | Decrease draft by one minute |
| +1 MIN | Increase draft by one minute |
| SAVE | Apply the draft and return to the utility |
| CANCEL | Discard draft and return |

D1 navigates actions; D0 activates. +1 MIN is selected initially so repeated D0
taps add minutes. Hold either button for Back/discard, or use the existing
logical Back event. OpenMenu also discards the draft. This supports the current
two buttons and the proposed Navigate/Action/Back model; no new GPIO is added.

Editing is a preview: actual durations and storage remain unchanged until Save.
A running/paused countdown keeps its current remaining/total time. New work
settings apply to subsequent starts; break settings apply to the next break.
Timer completion takes priority and closes the editor without saving. A mode
change, dock transition or reboot also leaves the editor without committing.

## Range and compatibility

- Minimum: one minute. Maximum: the existing seven-day timer limit (10,080 min).
- Values clamp at endpoints; they do not wrap to zero or another duration.
- Defaults remain 25-minute work and five-minute break.
- BLE command 04 (start), 0A (configure work/timer) and 22 (configure break)
  continue accepting seconds, including short diagnostic timers.
- Opening a second-resolution setting proposes rounding **up** to the next whole
  minute, without changing the setting until Save. Cancel retains exact seconds.
- Work and standalone/Cooking timers continue sharing the existing configured
  duration. Separate per-feature defaults are not added by this focused change.
- The editor itself introduced no BLE mode/command/packet change. **Today**
  work/timer and break durations are durable using coalesced **v5** writes;
  the v3/runtime-only description belongs to its original delivery branch.
- Both BLE timer duration commands retain their original second precision,
  while local edits still use one-minute steps.

The Focus utility adds Break at index 5, retaining prior action indexes 0–4.
Other timer utilities retain four actions. Editor appends screen ID 7. Simulator
JSON exposes `draft_duration_seconds`, `editing_break_duration`, `duration_action`.

This is the duration-editor portion of Local Settings, not a complete Settings
menu. Theme/audio/motion settings and third physical-button mapping are separate
follow-ups.

## Build and verify

Use the existing build command/profile for your hardware; no new SDK settings,
overlays or wiring are needed. For motion + microphone + OLED, from
`pico_firmware` in the SDK 3.4.2 terminal:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-motion -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_motion_tuner.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_tuner.conf;configs/xiao_motion.conf"
```

Flash `build-xiao-motion\zephyr\zephyr.uf2` using the existing UF2 procedure.

Passed locally: C++17 host suite with warnings as errors, all 20 simulator tests,
ASan/UBSan host suite (LeakSanitizer disabled), asset regeneration, Python syntax
and whitespace checks. New coverage includes ±60-second changes, endpoints,
preview/confirm/cancel, old second-resolution settings, running-timer isolation,
next-break application, persistence, Cooking and completion priority.

Target compilation and physical OLED/button validation remain unverified here;
no SDK toolchain or board is available. Check on hardware:

1. Focus -> Duration: add/subtract minutes, cancel, then save. Verify the result.
2. Focus -> Break: change five to six minutes and run a shortened work phase
   over BLE to check that the next break uses six minutes.
3. Edit during a timer; verify countdown continues and completion wins.
4. Set a ten-second duration over BLE, open the editor and cancel: keep ten
   seconds. Repeat and Save: explicitly convert to whole minutes.
5. Reboot after allowing the coalesced save; work duration survives, while break
   retains its documented runtime-only behavior.
