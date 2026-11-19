# Music reference pitches

Music now adds TONE beside Tuner. The eight Music actions are BPM-, BPM+, Start,
Tap, Bar, Tuner, Tone, Home. Existing indices 0–5 retain meaning; Home moves to 7.
The separate Reference screen has Note-, Note+, Play/Stop, Oct-, Oct+, Back.
Navigate chooses a control; Action activates it. Hold/Back stops and exits.

Reference defaults to A4=440 Hz and supports MIDI notes 40–84 (E2–C6), including
standard guitar's six open-string notes. Selection moves by one semitone or an
octave, clamped to the range. It shows note/octave and frequency in Hz. A play
request sounds a two-second triangle-wave reference with short attack/release,
then returns to Ready. It is on demand, not a continuous unattended oscillator.
Changing the selected note stops playback; press Play to hear the new pitch.
The reference is fixed equal temperament at A4=440, with no alternate calibration.

Playback requires the existing optional MAX98357/I2S audio profile. OLED-only
builds still show note/frequency and SPEAKER UNAVAILABLE, and refuse Play. A PWM
buzzer is not used as an inaccurate substitute. Mute or zero volume prevents
playback; unmuting does not auto-replay. Volume is snapshotted per sound job.
Small speakers may reproduce low fundamentals poorly; harmonics are expected.

Opening Tone stops the metronome and microphone tuner. Starting tuner or
metronome, leaving the reference screen, changing mode/dock, opening Settings,
muting, setting volume zero or a timer completion cancels the tone. Timer alerts
retain priority and playback is refused during their completion overlay.
The screen will not auto-sleep or accept Sleep Now while a reference is playing.
No arbitrary file/audio recording is involved and no microphone samples leave
the device.

## Core, PCM and platform

The portable audio module uses a fixed millihertz table and integer phase math.
It renders bounded, identical left/right 16-bit PCM at the existing 31,250 Hz
sample rate; peak is limited to 2048 before the existing 0–100% volume scaling.
Chunk boundaries do not change the waveform. The core owns the transient note,
play intent and two-second deadline; it does not own another Timer or worker.
Music service deadlines include reference expiry. Boot resets to A4 and stopped.

The existing I2S worker snapshots note, volume and a generation token in each
job. Main changes the reference request atomically; the worker checks mute and
generation before allocation and before submitting each block. Cancelled jobs
DROP rather than DRAIN, and queued stale tokens never replay, even if the same
note is requested again. Buffer ownership/recovery and bounded waits retain the
existing driver path. Actual cancellation latency includes queued DMA buffers,
work scheduling and driver waits and remains physically unmeasured. The main
thread submits intent without waiting for PCM rendering.

The platform reports runtime I2S readiness into Core. Main applies audio mute
and reference cancellation before synchronizing microphone capture intent.
The pre-existing PDM settling/drain behavior remains; physical feedback and
transition timing still need validation on the chosen combined profile.

No persistence format, mode ID, BLE command ID, UUID or status-packet change is
introduced. Reference cue appends existing cue IDs. ReferenceTone screen is 11;
10 is reserved for the separately reviewed Focus summary. Logical reference
EventKind IDs start at 55, reserving 53/54 for that Focus work. These internal
IDs are not new BLE commands. This Music branch is based on current main; the
separate Focus PR is preserved, and any source merge conflicts need reconciliation.

Simulator virtual AudioReady defaults true, just as virtual microphone readiness
does. It displays playback state and Chirp effects but does not play reference
sound through the desktop soundcard. Inject AudioReady 0 for the missing-speaker
path. JSON adds audio_ready, reference_note, reference_frequency_millihz and
reference_playing. Logical injection adds OpenReference, SetReferenceNote,
PlayReference, StopReference and AudioReady.

## Validation and usage

Run `bash tests/host/run.sh` and `bash simulator/run-tests.sh`. Tests cover PCM
frequency/zero crossings at E2/A4/C6, chunk equivalence, mute, bounds, stereo,
invalid notes, note change, deadline wrap/expiry, timer priority, tuner/settings
exit, missing speaker and restart. A production-worker model checks generation
cancellation, stale same-note rejection, normal drain and slab release against
an immediate-release fake I2S driver. It is not a Zephyr thread/DMA validation.
Existing GPIO/display adapter and core regressions remain included.

Use the same XIAO SDK 3.4.2 build profile. No additional wiring is needed to see
the tool. Sound uses the existing [I2S amplifier profile and wiring](sprint02.md);
check pin/profile compatibility before combining it with other optional adapters.
The ordinary OLED/mic/motion build does not enable the amplifier automatically.
Do not connect a speaker directly to a GPIO. No erase or bond clearing is needed.

Demo: Music -> Tone -> Play (A4), Note-/Note+ or octave controls -> Play,
Stop/Back. Try MIDI E2=40, A2=45, D3=50, G3=55, B3=59, E4=64 for standard guitar
reference, or A4=69. Tuner remains microphone input; Tone is optional audio output.

Target compilation, measured pitch/sample-clock accuracy, low-note speaker
response, acoustic level and live DMA/cancellation behavior are unverified here.
Bench power measurements remain deferred by request. Do not describe the
mathematical oscillator as a calibrated physical tuning standard.
