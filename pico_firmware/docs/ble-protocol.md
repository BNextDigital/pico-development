# Pico BLE protocol v1

**Current source milestone:** `0.9.0-oled-power` (November 18, 2026). The protocol byte and 16-byte status remain v1; additive command IDs do not imply the Flutter companion app implements them. See [companion compatibility](https://github.com/BNextDigital/pico-companion/blob/main/pico-companion/docs/firmware-compatibility.md).

Pico advertises one custom 128-bit service:

- Service: `8e7b0001-54f2-4d9b-a3d1-9f150000c001`
- Status: `8e7b0002-54f2-4d9b-a3d1-9f150000c001` (read + notify)
- Command: `8e7b0003-54f2-4d9b-a3d1-9f150000c001` (write)

Status and command access require an encrypted BLE link. The prototype uses
LE Secure Connections/Just Works bonding, so it prevents passive reading and
retains paired identities but does not yet authenticate that a human-approved
phone is the central. A production pairing ceremony can use Pico's screen and
buttons.

## Commands

All integers are little-endian. There is no packed C struct on the wire.

| Bytes | Meaning |
| --- | --- |
| `01` | Interact with Pico |
| `02 mm` | Set mode to `mm` |
| `03 mm` | Set mood to `mm` |
| `04 ss ss ss ss` | Start a timer for 1–604800 seconds |
| `05` | Cancel the running timer |
| `06` | Request a status notification |
| `07 tt` | Select theme ID 0–7; persists |
| `08` | Pause active timer |
| `09` | Resume paused timer |
| `0A ss ss ss ss` | Configure next timer duration, 1–604800 seconds; persists |
| `10` | Open menu |
| `11` | Next menu item / next theme |
| `12` | Previous menu item / previous theme |
| `13` | Select / activate highlighted action |
| `14` | Back / exit |
| `15` | Open local Settings |
| `20` | Start explicit work/break cycle when idle |
| `21` | Confirm next cycle phase while waiting |
| `22 ss ss ss ss` | Configure break duration, 1–604800 seconds; durable |
| `23 cc` | Test cue 0 interact, 1 timer done, 2 work done, 3 break done |
| `24 mm` | Durable mute: 0 unmuted, 1 muted |
| `25 vv` | Durable volume percent, 0–100 |
| `30` | Start metronome and open Music |
| `31` | Stop metronome |
| `32 bb bb` | Set BPM, 16-bit little-endian, 30–240 |
| `33 nn` | Set beats per bar, 1–8 |
| `34` | Tap tempo at processing time |
| `35` | Open/start onboard microphone tuner |
| `36` | Stop tuner listening |
| `40 ff` | Durable motion mask: pickup bit 0, rock bit 1, rest bit 2 (0–7) |

Mode IDs are companion `0`, focus `1`, music `2`, cooking `3`, wayfinding `4`,
notes `5`, and driving `6`. Driving cannot be requested remotely; the car dock
owns that transition.

Mood IDs are neutral `0`, happy `1`, curious `2`, sleepy `3`, and alert `4`.

## Status packet

The status characteristic is always exactly 16 bytes.

| Offset | Size | Value |
| ---: | ---: | --- |
| 0 | 1 | Protocol version (`1`) |
| 1 | 1 | Mode ID |
| 2 | 1 | Mood ID |
| 3 | 1 | Dock ID: none `0`, home `1`, car `2` |
| 4 | 1 | Flags: bit 0 connected, bit 1 timer running, bit 2 timer paused, bit 3 cycle waiting, bit 4 muted, bit 5 metronome running, bit 6 tuner listening, bit 7 motion configured and ready |
| 5 | 1 | Battery percentage, or `255` when unknown |
| 6 | 4 | Timer seconds remaining |
| 10 | 4 | Lifetime interaction count |
| 14 | 2 | Capability bit field |

Capability bits are display `0`, speaker `1`, microphone `2`, home dock `3`,
car dock `4`, position `5`, compass `6`, and motion `7`.

Unknown commands, wrong lengths, zero-duration timers, invalid enum values,
and attempts to request driving mode are rejected at the GATT boundary.

## Sprint 01 behavior and compatibility

Original command IDs 01–06, mode IDs, UUIDs, and the 16-byte status shape are
unchanged. Clients must ignore unknown flag bits; paused timers clear running
bit 1, set bit 2 and retain remaining/total duration. A paused timer does not
elapse. Cancel handles paused timers as well as running timers.

Theme IDs: Modern 0, Chibi 1, Anime 2, Retro IBM 3, Mecha 4, Vintage Cartoon 5,
Minimal Emoticon 6, Kawaii Mascot 7. Theme and configured duration are durable;
transient expressions and active timers are not. `03` reactions last four
seconds. `01` remains a pat and never starts a focus timer.

The XIAO waits for phone-initiated Bond rather than automatically requesting
security on connect; encrypted GATT access and Secure Connections requirements
are preserved. Status reads/commands still require pairing/encryption.

Example ten-second utility demonstration: `10`, `13`, `0A 0A 00 00 00`,
`13` (Start), `08` (Pause), `09` (Resume), `05` (Cancel).

## Sprint 02 additions

Commands 20–24 are additive; the 16-byte v1 status, all prior IDs and UUIDs stay
unchanged. Clients must mask flags and ignore unknown bits. Bit 3 indicates a
cycle waiting for confirmation, with running/paused clear. The packet does not
encode work versus break or completed-session count; an app needing those
values should use a future additive status extension rather than infer them
from a stale command history.

StartCycle refuses to replace a running/paused timer; Next refuses to skip an
active phase. These syntactically valid no-op commands are accepted by GATT;
observe status to learn the resulting state. `05` cancels waiting cycles too.
`04` explicitly replaces a cycle with a standalone timer; `01` remains a pat.
At Sprint 02, break duration and mute were volatile. **On current `main`**, break,
mute, volume and motion mask persist through storage v5 along with theme,
work duration, OLED brightness and screen timeout.
The test-sound command generates the existing Chirp effect without changing
mood/count. A muted or unavailable output may produce no audible cue.

## Music metronome additions

Music remains mode ID 2. Commands 30–34 are additive. Status v1 remains 16 bytes;
flag bit 5 reports metronome running, while BPM/bar count remain on-device or
simulator state (not encoded in this packet). Clients must ignore unknown flag
bits. Tempo/bar settings are volatile; reboot stops playback and restores
120 BPM/four beats. Start refuses in a car dock; duplicate Start does not reset
phase. Beats do not send notifications; start/stop/settings changes do.
See [music-metronome.md](music-metronome.md) for controls and timing limits.

## Tuner additions

Commands 35/36 and flag bit 6 are additive. Microphone readiness uses the existing
capability bit 2; the unchanged status packet does not encode note/cents/level.
A start without a configured microphone opens an unavailable screen and leaves
listening clear. Back, mode/dock changes and timer completion stop listening.
Pico's sound output is suppressed while listening; the user mute flag is unchanged.
No audio or pitch readings are streamed over BLE. See [music-tuner.md](music-tuner.md).

## Motion personality additions

Command 40 configures a three-bit behavior mask (default 7 on fresh storage).
The mask was originally volatile; it is **durable in current storage v5** after
a confirmed change and coalesced save.
Status flag bit 7 means the IMU is ready with a nonzero mask; capability bit 7
means motion hardware is available. The 16-byte v1 status does not encode the
mask or raw acceleration. Motion reactions do not control utilities, timers or
modes and do not themselves persist. The user-configured behavior mask does
persist. Clients must ignore unknown flags. See
[motion-personality.md](motion-personality.md) for context guards and tests.

## Local Settings additions

Commands 15 (open Settings) and 25 (volume byte 0–100) are additive. UUIDs, modes
and status v1 stay unchanged. Break duration, mute, volume and motion mask now
persist through storage v4, as do theme/work duration. Previous runtime-only
notes describe their introduction; the v4 behavior supersedes them. The packet
has no new settings readback fields. See [local-settings.md](local-settings.md)
for editor controls, migration and downgrade behavior.

OLED brightness/timeout are local settings in storage v5; they add no BLE command
IDs or fields to the existing status packet. Logical button commands share the
wake-only first-input rule when the display is asleep. Timer completion wakes
the display. See [OLED power policy](oled-power.md).

## Cross-repository consumer status

The Flutter companion app currently encodes only `01`–`06` and decodes the
original connected/running flags and capabilities through compass bit 6.
Firmware's pause/wait/mute/metronome/tuner/motion flags and motion capability
bit 7 are not yet represented in app models. Unknown bits do not change the
16-byte wire size. The app must not infer missing settings, tempo or pitch
readback from command writes; those fields are not encoded in status v1.

## Reference pitches

Reference pitches add no BLE IDs or status fields. Existing mode IDs and
persistence v5 remain unchanged. Note selection and playback have local UI and
simulator logical injection only.
See [Music reference pitches](music-reference.md).
