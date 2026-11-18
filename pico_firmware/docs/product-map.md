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

| Mode/context | Intended behavior | Firmware status in 0.1 |
| --- | --- | --- |
| Companion | Face, mood, interactions, lightweight local memory | Core behavior implemented |
| Study/focus | Tomato-style focus timer, water breaks, later quizzes | Reliable general timer implemented |
| Music | Chromatic tuner from microphone; speaker metronome | Stable mode ID; audio hardware pending |
| Cooking | Egg/general timer and concise cooking reference | Stable mode ID; timer foundation ready |
| Wayfinding | Compass/GPS or phone-assisted direction cues | Stable mode ID; sensors pending |
| Notes | Local capture with later local speech-to-text handoff | Stable mode ID; microphone pending |
| Car dock | Driving-safe behavior and privacy-conscious parking pin | Dock transition implemented; location flow pending |
| Home dock | Reminders, timers, voice notes, internal dock illumination | Dock identity implemented; dock electronics pending |
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
