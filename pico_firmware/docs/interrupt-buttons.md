# Interrupt-driven interaction buttons

This roadmap step removes idle sampling of the D0 Action/Focus, D1 Navigate,
and optional D2 Back inputs. No wiring, devicetree aliases, BLE commands or
persisted settings change. The root icon menu and existing utilities remain.
Legacy `sw0`/`sw1` press-edge behavior and dock interrupts are unchanged.

## Design

`pico::Button` exposes its next debounce/hold deadline as a relative delay.
It remains deterministic, clock-independent and compatible with simulator
sampling. Thirty milliseconds debounce and one second hold remain unchanged.
A hold emits once and never becomes a tap on release.

`platform::GpioButton` owns the GPIO callback, portable classifier and delayable
work. Both-edge interrupts submit immediate work; they neither read the pin
nor dispatch core events. The system workqueue reads logical active state
(including active-low configuration), classifies it and enqueues the existing
event through EventSink. Only that worker accesses classifier state.

After a sample, work is scheduled only for a pending debounce or hold deadline.
Idle and already-held inputs have no recurring work. A startup sample detects
an input held during boot. GPIO configuration/interrupt failure leaves that
input unavailable; optional Back failure preserves two-button hold-to-back.
Read errors reset the classifier without producing a false release/tap; the
next edge retries. Application queue overflow retains the existing logged-drop
policy. Button objects have static lifetime and are initialized once at boot.

The worker uses `k_work_schedule` for its future deadline, preserving an edge
already queued during execution. The interrupt uses `k_work_reschedule` to
replace a distant hold deadline with an immediate sample. See Zephyr's
[workqueue scheduling semantics](https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html).
Workqueue latency still affects observed press timing; very short pulses or
multiple edges before a sample may coalesce. This is a human-button interface.

## Validation

`bash tests/host/run.sh` now also compiles the production GPIO button adapter
against a small fake GPIO/workqueue model. Tests cover startup/idle reads,
bounce, exactly-once hold, release-after-hold, held-at-boot and uptime wrap,
a release interrupt racing with the worker, independent three-button events,
read failures and init cleanup. Portable tests also verify deadline boundaries.
Run `bash simulator/run-tests.sh` for the existing core/UI/protocol regressions.

Local checks pass: the host core suite and GPIO adapter model, all 24 simulator
tests, ASan/UBSan runs of both host executables, packed face asset regeneration
and whitespace validation. Leak detection was disabled for the sanitizer runs.

The fake workqueue tests exercise our adapter and the documented API contract;
they do not validate Zephyr internals, Nordic interrupt allocation, electrical
bounce or a successful target build. The nRF SDK is unavailable here.

## Physical build and acceptance

Use the same SDK 3.4.2 terminal, wiring and XIAO profile as the working firmware.
For the combined OLED/mic/motion profile, from `pico_firmware`:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-motion -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_motion_tuner.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_tuner.conf;configs/xiao_motion.conf"
```

Copy `build-xiao-motion/zephyr/zephyr.uf2` to the bootloader drive. Keep your
existing optional-third-button or audio overlay additions if you use them.
No erase, settings reset or bond clearing is required. Bring-up diagnostics
remain available.

1. Confirm button interrupt-ready logs and BLE reconnect after startup.
2. Tap Action and Navigate repeatedly, including fast alternating inputs.
   Check one response per deliberate tap and normal menu/utility selection.
3. Hold each input past one second. Confirm one hold action, no repeats while
   held, and no extra tap on release. Test Back/Cancel in a settings preview.
4. Reboot with Action held and confirm its hold action; repeat with Navigate.
5. If fitted, test D2 Back independently and with other buttons. Without D2,
   confirm both existing hold-to-back paths still operate.
6. Run timers, metronome and tuner with BLE traffic while pressing/holding.
   Check for missing events, delays, stuck states and unexpected resets.

## Power limits and next roadmap steps

This step removed continuous *button* reads while retaining the 20 ms main-loop
timeout. The subsequent [deadline-sleep change](deadline-sleep.md) replaces
that fixed timeout with service deadlines.
No current reduction or battery runtime has been measured. Do not describe
this build as full interrupt-only sleep or battery-ready firmware.

The deadline-sleep change preserves music/sensor latency. Next add measurable
peripheral idle policy and amplifier shutdown with
confirmed hardware; then measure whole-device idle/connected/active current
before selecting battery policy. OLED contrast and explicit display sleep can
follow with wake/alert priority tests. Location, notes and peer features remain
later work requiring their own hardware/privacy scope.
