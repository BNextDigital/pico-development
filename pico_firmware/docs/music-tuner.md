# Music tuner: onboard Sense microphone

Firmware `0.5.0-tuner`, branch `feature/music-tuner`, based on main `7b9baea`
after the metronome merge. Implemented portable pitch detector, tuner UI and
optional Zephyr capture adapter. Target compilation and physical mic operation
have **not** been validated here.

## Build and flash on your XIAO nRF52840 Sense

No new wiring: the Sense board already connects PDM clock P1.00, data P0.16
and microphone power enable P1.10. The non-Sense XIAO lacks this microphone.
OLED and D0/D1 wiring remain unchanged. No amplifier or speaker is needed.

From `pico_firmware` in the nRF Connect SDK 3.4.2 terminal, paste as one line:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-tuner -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_tuner.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_tuner.conf"
```

Double-reset and copy **build-xiao-tuner\zephyr\zephyr.uf2** to the UF2 drive.
Keep the previous OLED UF2 for rollback. No mass erase or bond clearing.
This is the first target-build test for the new adapter: if it fails, collect
the first compiler/Kconfig/devicetree error, not only the final Ninja summary.

The old OLED-only build still works and displays MIC UNAVAILABLE when Tuner is
selected. Its overlay does not enable PDM. The tuner overlay includes the OLED
layout; do not additionally list the OLED overlay. This profile enables DMIC,
fixed regulators and FPU context sharing. The board's other boot-on regulators
may also become active; no power/current/battery claims are made.

## Use

**Menu -> MUSIC -> TUNER.** D1 moves, D0 selects. Music now has seven actions:
BPM−, BPM+, Start/Stop, Tap, Bar, Tuner, Home. Its controls use a four-column grid.
Opening Tuner stops the metronome and starts microphone capture if configured.

Pluck one string and let it ring; damp the other strings. The display shows:

- Nearest chromatic note and octave (A4 = 440 Hz; middle C is C4).
- Signed cents, a centered meter, and FLAT / SHARP / IN TUNE (within ±5 cents).
- An uncalibrated relative signal-level bar, not dB SPL.
- PLUCK ONE NOTE while settling/confirming, TOO QUIET, HOLD ONE NOTE for
  unreliable periodicity, TOO LOUD for clipping, or MIC UNAVAILABLE.

A new note requires two consecutive matching estimates; subsequent estimates
use a three-frame median for cents. Silence/noise/clipping clears confirmation
and hides the previous note. Typical acquisition includes 50 ms power settling,
two discarded PCM frames and at least two analysis frames (~0.6 s plus processing
and UI delays). Actual response time must be measured on hardware.

Tap D0 on the tuner screen to stop/restart listening. Hold either button to
return to Music's parent menu. Leaving Music, changing dock context, rebooting,
starting the metronome or timer completion stops the tuner. A background
standalone timer can still complete and take priority.

Capture runs only while Tuner is listening. Stop is asynchronous: the worker
finishes its current read/analysis, stops DMA, drains buffers, then disables the
microphone's power regulator. If DMA buffers never return, capture is disabled
until restart and power is retained rather than declaring a safe power-off.
All Pico audio is temporarily muted during listening, without changing the
user mute setting; queued cues are cleared, and an already playing cue can
finish. This prevents Pico's own click/chirp from driving its tuner.

## Range and limits

The detector targets **35–1400 Hz**, covering standard guitar, low B/Drop C and
bass E1 in principle. Synthetic tests do not establish usable acoustic range,
accuracy or sensitivity of the physical microphone. Quiet bass fundamentals,
room noise, other instruments and dominant overtones may give weak estimates
or octave errors. This is a monophonic chromatic tuner, not chord detection.

A4 is fixed at 440 Hz. Custom concert pitch, alternate-tuning targets, strobe
view and calibration are not implemented. Verify real notes against a trusted
tuner before relying on the cents reading.

## Architecture and resources

`pitch::Detector` is allocation-free portable C++17. It uses a YIN-style
cumulative mean normalized difference, first credible local minimum and
parabolic interpolation of the raw difference around that minimum. It rejects
low AC energy, clipping and low-periodicity frames. DC is removed for level
gating and cancels in the difference function. Note/cents conversion uses a
short near-unity logarithm series and semitone ratios; no FFT, heap, or libm
runtime calls are required by the detector.

The Sense profile pins PDM clock to **1 MHz** and uses the hardware's **64:1**
ratio for **15,625 samples/s**, with HFXO as the clock source. The detector uses
that same rate, rather than assuming an approximate 16 kHz setting. Mono signed
16-bit frames contain 2048 samples (~131 ms). PDM left-channel selection follows
the initial board bring-up configuration and requires physical confirmation.

The platform worker has a 4 KiB stack with FPU context, a four-block 16 KiB DMA
slab, a ~1.8 KiB detector workspace and a one-result mailbox. It never performs
DSP in a BLE callback or main UI thread. Frames remain in volatile RAM and are
freed after analysis; no PCM is stored to flash, sent over BLE or uploaded.

Only compact note/cents/level/status results enter the existing event queue.
An 8-bit session token rejects readings from a recently closed tuner session.
Per-frame updates request Render only, not flash saves or BLE notifications.
The normal bring-up profile logs signal/level/note/cents and analysis time about
every two seconds while listening. Quiet profile suppresses those diagnostics.
Measured analysis time must stay below the ~131 ms frame interval for continuous
capture without backlog; target CPU/RAM/stack usage remains unmeasured.

Read/configuration failures stop capture and clear readiness; the rest of Pico
continues operating. DMA shutdown is bounded and buffers are drained before
power is removed. The microphone capability bit reflects readiness. No firmware
persistence format changes; reboot returns to Companion without listening.

## BLE and simulator

Existing encrypted service/characteristics remain unchanged:

| Command | Meaning |
| --- | --- |
| `35` | Open/start tuner; unavailable mic opens an explanatory screen |
| `36` | Stop listening |
| `14` | Back/exit and stop capture |

The unchanged 16-byte v1 status adds flag bit 6 for tuner listening. Microphone
capability remains bit 2. The v1 packet does not carry note/cents/level data;
a future app tuner view requires an additive status extension. TestSound is
ignored during listening. IDs, UUIDs, persistence v3 and old flags are preserved.

Simulator provides a virtual ready microphone, Tuner, A4 input, Silence and
Stop mic buttons. The backend command `tone MILLIHZ` creates signed mono PCM and
runs the **same production detector**, including confirmation and filtering.
Examples: `tone 440000`, `tone 65406` (C2), `tone 0` (silence). Tone injection does
not acquire the host computer's microphone or play audio. It is explicit frame
injection: the last result holds until another frame is injected.

JSON adds microphone readiness, tuner running, signal ID (Listening 0, Quiet 1,
Noisy 2, Clipping 3, Detected 4, Unavailable 5), MIDI note, signed cents and level.
Tuner appends screen ID 6; no prior screen/event IDs are renumbered.

## Validation

Passed locally:

- Full host suite with C++17 and warnings as errors.
- All 16 simulator tests, including menu/button/BLE and actual generated PCM.
- ASan/UBSan host suite (LeakSanitizer disabled in this runner).
- Face asset regeneration, Python syntax and whitespace checks.
- Framebuffer preview inspection.

Pitch tests cover each semitone D1–E6, selected bass/guitar notes at ±30 cents,
stronger second harmonics plus DC offset, decay plus noise, silence, clipping,
invalid frame sizes and high out-of-range tones. Tested clean synthetic fixture
errors stay within five cents; that is not a measured hardware accuracy spec.
Lifecycle tests cover unavailable mic, stale tokens, timer priority, dock/mode
exits, packet validation and framebuffer bounds. No real microphone recording
fixtures are available yet.

**Not validated:** SDK target build, microphone channel/power behavior, actual
sample rate, capture under BLE/OLED load, acoustic note accuracy, startup/pop
rejection, DMA recovery, current draw, memory/stack headroom or live GUI layout.
The SDK/toolchain and physical board are unavailable in this environment.

First physical test:

1. Build/flash the tuner profile. Confirm `Sense microphone ready: 15625 Hz mono`
   in the serial log, then Menu -> Music -> Tuner.
2. Check the level bar responds to a nearby plucked string. If silent, paste the
   mic diagnostic lines; channel/power/gain need confirmation before changing DSP.
3. Compare A4 (440 Hz) and each open guitar string against a trusted tuner,
   including deliberate sharp/flat changes. Add low B/Drop C and bass if available.
4. Damp all strings: previous note should disappear. Test a noisy room, a chord
   and an overly loud source; ambiguous notes must not be treated as proof of accuracy.
5. Stop/restart repeatedly, hold to exit, change modes, reconnect BLE, run a
   timer, and reboot. Confirm no stuck capture, queue overflow or slab errors.
6. Record diagnostic `analysis_ms`, responsiveness and RAM/stack data from the
   target build. Capture measured pitch offsets before adding calibration.

Source references reviewed at SDK `ncs-v3.4.2`:
[Sense board](https://github.com/nrfconnect/sdk-zephyr/blob/ncs-v3.4.2/boards/seeed/xiao_ble/xiao_ble_nrf52840_sense.dts),
[board pinctrl](https://github.com/nrfconnect/sdk-zephyr/blob/ncs-v3.4.2/boards/seeed/xiao_ble/xiao_ble-pinctrl.dtsi),
[DMIC API](https://github.com/nrfconnect/sdk-zephyr/blob/ncs-v3.4.2/include/zephyr/audio/dmic.h),
[nrfx DMIC adapter](https://github.com/nrfconnect/sdk-zephyr/blob/ncs-v3.4.2/drivers/audio/dmic_nrfx_pdm.c),
[DMIC sample](https://github.com/nrfconnect/sdk-zephyr/blob/ncs-v3.4.2/samples/drivers/audio/dmic/src/main.c),
[PDM clock calculation at SDK's pinned HAL](https://github.com/zephyrproject-rtos/hal_nordic/blob/18da0cc9726f8759c627dba3180b3ba9294e433c/nrfx/drivers/src/nrfx_pdm.c).
