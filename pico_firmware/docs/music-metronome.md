# Music: visual metronome and tap tempo

Firmware `0.4.0-music`, branch `feature/music-metronome`, based on main after
Sprint 02 (merge `60be85a`). This is Pico's first Music utility. It runs offline
on the existing XIAO/OLED/buttons; the amplifier is optional.

## On the device

Open **Menu -> MUSIC**. D1 moves through six actions, D0 activates the selected
action. START is selected initially.

| Action | Behavior |
| --- | --- |
| BPM− | Decrease by 5, minimum 30 |
| BPM+ | Increase by 5, maximum 240 |
| START / STOP | Toggle metronome; start immediately flashes beat 1 |
| TAP | Tap D0 repeatedly along with a song to estimate BPM |
| BAR | Cycle 1–8 beats per bar; default 4 |
| TUNER | Open optional onboard microphone tuner; stops metronome |
| HOME | Stop and return to Companion/Home |

The screen shows BPM, numbered beat position and one box per beat. The current
box fills briefly on each beat. The first beat gets a higher-pitched accent if
I2S audio is fitted/enabled. Runtime mute preserves visual beats.

For the tuner profile see [music-tuner.md](music-tuner.md).

Holding either button goes back to the menu and stops the metronome. Leaving
Music through another mode, opening the menu, changing dock context, or rebooting
stops it. Selecting MUSIC again preserves the last highlighted Music action.

Tap tempo averages the last four valid intervals (the second accepted tap is
enough for an initial estimate). Intervals shorter than 250 ms are ignored;
gaps over two seconds start a fresh series. The range is 30–240 BPM, rounded to
the nearest integer. Tapping sets tempo and does not automatically start playback.
BPM changes during playback preserve fractional beat progress.

BAR counts equal pulses; it does not select a time-signature denominator,
subdivisions or swing. Tempo and bar count are runtime settings, reset to
120 BPM / four beats on reboot. Persistence v3 is unchanged.

A standalone timer may remain active in the background. Timer completion stops
the metronome and takes priority with the existing alert. Changing away from
Focus still cancels a work/break cycle, as in Sprint 02.

## Build / flash

From `pico_firmware` in the SDK 3.4.2 terminal, paste as one line:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_ssd1306.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf"
```

Flash `build-xiao\zephyr\zephyr.uf2` using the existing double-reset/UF2 method.
The OLED-only build is sufficient for Music. Optional audio uses the existing
[Sprint 02 audio profile and wiring](sprint02.md); no new pins are required.
The fixed PWM fallback chirp is intentionally not used as a metronome click.

## BLE and simulator

Existing service/characteristics and encrypted permissions are unchanged.

| Hex command | Action |
| --- | --- |
| `30` | Start metronome and open Music screen |
| `31` | Stop metronome |
| `32 78 00` | Set 120 BPM (unsigned 16-bit little-endian, range 30–240) |
| `32 89 00` | Set 137 BPM |
| `33 03` | Set three beats per bar (range 1–8) |
| `34` | Tap tempo at command-processing time |
| `24 01` / `24 00` | Existing runtime mute / unmute |

The unchanged 16-byte v1 status adds bit 5 for metronome running. It does not
encode tempo or bar count. Tempo is visible on-device and in simulator JSON;
future app readback requires an additive status extension. Invalid lengths and
out-of-range tempo/bar values are rejected. Old IDs, mode IDs and cue IDs 0–3
remain stable. TestSound `23` still accepts only the four existing public cues.
Internal metronome cue IDs 4/5 are not exposed as TestSound commands.

Simulator adds Metronome, Stop beat, Tap tempo, a BPM input and Set BPM.
Generic events also expose `SetMetronomeBeats`, `StartMetronome`, `StopMetronome`
and `TapTempo`. Use `advance_ms 20` to inspect sub-second timing. The GUI advances
in 20 ms steps while playing, otherwise 100 ms; accelerated time intentionally
skips stale beats. Desktop audio playback is not implemented.

JSON adds `metronome_running`, `metronome_bpm`, `beats_per_bar`, `current_beat`,
`beat_pulse` and `music_index`. Music screen appends ID 5 without renumbering
existing screens/events.

## Timing and resources

`Metronome` is a portable allocation-free service. Its phase accumulates
`elapsed_ms * BPM` modulo 60,000, avoiding rounded-period drift at e.g. 137 BPM.
Unsigned millisecond differences support the 32-bit clock rollover; updates
must occur within one complete clock wrap (~49 days). A delayed update advances
to the correct current beat and emits at most one click, never a catch-up burst.
The visual pulse is nominally 80 ms.

`MusicTick` carries fresh monotonic milliseconds independently of the slower
personality animation. The existing main loop polls roughly every 20 ms;
beats do not request flash saves or BLE notifications. Rendering changes only
at pulse edges, beats or user actions, and the existing framebuffer cache skips
identical OLED writes. Chirps enqueue before OLED transfers.

The optional I2S worker renders short bounded click/accent scores. It drops
beats while audio is busy rather than queueing late clicks behind other cues;
stopping suppresses queued metronome cues (an already playing <=16 ms click can
finish). Completion alerts keep the existing priority and bounded queue.

This is a **prototype metronome**: mathematical phase accuracy does not prove
low hardware jitter. OLED I2C transfers, polling, diagnostics and audio-worker
scheduling can delay a visible/audible beat. Physical onset/jitter must be
measured before treating it as a recording/practice timing reference. A future
hardware-timed audio stream can use the same tempo/control model.

## Validation and hardware checks

Passed locally:

- Host regression suite under C++17 with warnings as errors.
- All 14 simulator tests, including actual menu/button and raw BLE paths.
- ASan/UBSan host run (LeakSanitizer disabled in this runner).
- Face asset regeneration, Python syntax and whitespace checks.

New tests exercise non-integral BPM, one-hour phase progression, rollover,
delayed updates without burst, tap averaging/bounce/timeout, local controls,
mode/dock exits, timer completion priority, packet boundaries, status bits and
visible beat changes without audio. The old Music-unavailable expectation now
checks the implemented Music screen.

**Unverified:** target compilation for this revision, live OLED readability,
button tap feel, audio onset/jitter and recovery under BLE/diagnostic traffic.
No SDK toolchain, attached board or live GUI is available here. The earlier
OLED/menu/work-break physical results belong to the prior firmware revision.

On hardware:

1. Open MUSIC, start at 120 BPM and verify alternating beat boxes and bar wrap.
2. Stop/start, set 30 and 240 BPM, and cycle BAR through 1–8.
3. Select TAP, tap a steady rhythm and verify the displayed estimate; wait over
   two seconds, then tap a different rhythm to restart the estimate.
4. Hold to exit and verify playback stops. Check Companion, themes and Focus
   work/break flows still work after leaving Music.
5. If audio is fitted, check the first-beat accent and mute; compare click onset
   against a trusted reference while BLE is connected and diagnostics run.
6. Set tempo/bar, reboot and confirm Companion/Home with playback stopped;
   durable face theme/work duration should still survive.

Music follow-ups: reference pitches once audio is physically verified, and rhythmic
subdivisions after onset/jitter measurement. The optional tuner implementation
is documented separately in [music-tuner.md](music-tuner.md); physical PDM
capture and acoustic accuracy remain unverified. No recording feature is implemented.
