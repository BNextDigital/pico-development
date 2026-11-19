# Pico product map carried into firmware

## Product thesis

Pico is a small, friendly, offline-first companion that lets someone keep a
useful thread of connection without defaulting to their phone. It is closer to
an AirTag-sized virtual companion with situational tools than a tiny general-purpose phone.
Personality is part of the utility: the same object that remembers where the
car is can react, keep small local rituals, and notice another Pico nearby.

The device should remain useful without an account or server, tolerate the
companion app disappearing, and avoid turning into e-waste when a battery ages.
BLE is the ordinary phone link; the phone supplies heavyweight services only
when a feature genuinely needs them.

## Modes and contexts

| Mode/context | Intended behavior | Current firmware status |
| --- | --- | --- |
| Companion | Face, mood, interactions, lightweight local memory | Themes, deterministic idle expressions, blinking and motion reactions implemented |
| Study/focus | Tomato-style focus timer, water breaks, later quizzes | Work/break cycles, pause/resume and local minute-duration controls implemented |
| Music | Chromatic tuner from microphone; speaker metronome | Metronome, optional microphone tuner and optional I2S reference pitches implemented; physical validation remains profile-specific |
| Cooking | Egg/general timer and concise cooking reference | Local configurable timer implemented; cooking references remain future work |
| Wayfinding | Compass/GPS or phone-assisted direction cues | Stable mode ID; sensors pending |
| Notes | Local capture with later local speech-to-text handoff | Stable mode ID; capture/storage/handoff not implemented |
| Car dock | Driving-safe behavior and privacy-conscious parking pin | Dock transition implemented; location flow pending |
| Home dock | Reminders, timers, voice notes, internal dock illumination | Dock identity implemented; reminders/capture and dock electronics pending |
| Pico peer | Nearby-Pico detection and playful generated interaction logs | Later BLE observer/privacy design |

## Guardrails

- Pico is not a fitness tracker, heart-rate monitor, or activity-scoring device.
- No haptic motor is assumed by the hardware or firmware.
- Car mode must not create reckless prompts or attention-heavy interactions.
- Location should be stored and shared deliberately, not continuously leaked to
  a platform provider.
- Core actions must work locally; BLE failure cannot make the face or buttons
  useless.
- The prototype uses simple through-hole indicators instead of committing to
  addressable LED hardware.
- Optional modules must fail absent. A missing microphone, display, dock, or
  speaker cannot prevent the rest of the device from booting.

## Firmware consequence

The architecture has a small permanent core and replaceable feature modules.
Feature activation is data sent over the versioned protocol; feature code is
part of a signed firmware build. This preserves the “card slash” feel discussed
for the app/device relationship while keeping executable code, memory limits,
and hardware access under firmware control.

## Next development priorities

The icon root menu, Local Settings, interrupt-driven buttons and deadline waits
are implemented. OLED brightness and screen sleep extend the power foundation;
[OLED controls](oled-power.md) document physical acceptance and migration.

1. Validate OLED contrast/blanking, wake/alert priority and whole-device current
   with the actual active hardware profiles and diagnostics disabled.
2. Add peripheral idle/shutdown policy with confirmed amplifier wiring. Measure
   idle, connected, motion, metronome and tuner consumption separately.
3. Finalize battery hardware, monitoring and policy using those measurements.
4. Scope Notes capture/storage or Wayfinding hardware separately; their menu
   icons do not imply those features are implemented.

Host success alone does not validate target compilation, OLED readability,
acoustic tuner accuracy, audio output, electrical behavior or battery runtime.

The current feature pass adds [Music reference pitches](music-reference.md).
It uses the existing optional audio adapter; power bench work remains deferred.
Focus session goals/history are a separate reviewed change and are not removed
or overwritten by this branch. Durable Music calibration/presets and broader
practice tools remain future scope.
