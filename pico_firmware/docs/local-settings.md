# Local Settings

**Current source (November 18, 2026):** storage v5 and the OLED-power follow-up extend the original v4 Settings implementation. Earlier implementation/test sections below describe their original milestone.

Firmware `0.7.0-local-settings`, continuing the minute-editor branch/PR. Adds a
Settings hub, shared draft editors, durable preferences and optional third-button
input. No new hardware is required for the existing two-button setup.

## On Pico

Open **Menu -> SETTINGS**. D1 moves through items; D0 selects. The hub contains:

| Item | Local behavior |
| --- | --- |
| Face theme | Preview any of the eight actual bitmap themes, then Save/Cancel |
| Mute | Preview on/off, then Save/Cancel |
| Volume | Adjust by five percentage points, 0–100, then Save/Cancel |
| Work/Timer | Adjust in one-minute steps, then Save/Cancel |
| Break | Adjust in one-minute steps, then Save/Cancel |
| Motion | Preview pickup/rock/rest combinations, then Save/Cancel |
| Test sound | Request the short timer-completion cue at saved mute/volume |
| Brightness | Preview 5–100% in five-point steps; Save or Cancel |
| Screen Time | Off or 1–30 minutes; Save or Cancel |
| Sleep Now | Blank display and return Home; next button wakes without acting |
| Back | Return to the main menu |

Each editor starts with + selected. Navigate cycles − / + / Save / Cancel;
Action activates the highlighted control. Back or Cancel discards the draft.
No flash writes occur during preview. Theme preview uses the actual face assets;
it does not change the saved Home face until confirmed. Mute and motion settings
do not take effect until Save. Volume can be heard through Test sound after Save.

Work/Timer and Break use the minute editor from the previous change, clamping
from 1 minute to the existing seven-day maximum. Work and standalone/Cooking
still share the existing configured duration. BLE can retain exact seconds;
entering the editor only proposes rounding up, and Save confirms conversion.

Motion combinations show three explicit ON/OFF labels. +/− walk the eight bit
combinations: off, pickup, rock, pickup+rock, rest, pickup+rest, rock+rest, all.
The saved setting is respected on reboot, including all-off. A profile without
the IMU can still save these preferences for a later hardware-enabled build.

Opening Settings stops metronome/tuner playback. An already running/paused timer
continues in the background. Changing a configured duration does not restart
that timer; it applies on the next start/phase. Completion closes an open editor
without saving and keeps its alert priority. Mode/dock changes and reboot also
abandon drafts. Settings changes do not start a timer or capture microphone data.

The main menu adds Settings at index 7, moving Back to index 8; mode IDs and
wire commands are unchanged. The old Face Themes shortcut remains available
with its existing immediate-selection behavior. New screens append Settings=8
and SettingEdit=9; earlier screen IDs remain stable.

## Two buttons or optional third

| Action | Current hardware | Optional third-button setup |
| --- | --- | --- |
| Navigate | D1 tap | D1 tap |
| Select/confirm | D0 tap | D0 tap |
| Back/discard | Hold D0 or D1 | D2 tap (holds remain available) |

The optional switch connects **D2/P0.28 to GND**, with the internal pull-up.
Unplug USB before adding wiring. Use `overlays/xiao_third_button.overlay` after
your normal base overlay to add the `pico-back-button` alias. A missing third
button does not disable the two-button hold mappings. No third button is assumed
by default. SDK's XIAO connector mapping was checked for D2/P0.28.

Back is a navigation/cancel-edit action. It does not cancel a Home timer; use
that utility's explicit Cancel action. Home Action only pets; Home holds do not cancel.
The existing debounce/hold classifier is reused, so a hold emits one Back and
its release does not produce a second action.

## Durable settings: current storage v5

Confirmed theme, count, work/timer and break durations, mute, volume, motion
mask, OLED brightness and display timeout are durable. A change schedules a
coalesced save (five-second default), not a write per preview step. Wait for
the save window before disconnecting power. Active timers and editor drafts
are deliberately not restored; every boot enters Companion/Home.

| Record offset | Size | Current v5 value |
| --- | --- | --- |
| 0–3 | 4 | `PICO` magic |
| 4 | 1 | Storage version 5 |
| 5–7 | 3 | Legacy mode/mood/theme fields |
| 8–11 | 4 | Lifetime interaction count |
| 12–15 | 4 | Work/timer duration in seconds |
| 16–19 | 4 | Break duration in seconds |
| 20 | 1 | Mute flag |
| 21 | 1 | Volume percent (0–100) |
| 22 | 1 | Motion behavior mask (0–7) |
| 23 | 1 | OLED brightness percent (5–100) |
| 24–27 | 4 | Screen inactivity timeout (0=Off, 1–30 minutes) |
| 28–31 | 4 | FNV-1a checksum over bytes 0–27 |

The decoder accepts prior v1/v2 (16 bytes), v3 (20 bytes), v4 (28 bytes),
and v5 (32 bytes), with validation and in-memory migration. Defaults for older
missing values: break 300 seconds, unmuted, volume 25%, motion mask 7,
brightness 50%, timeout Off. The next confirmed durable change writes v5;
automatic reactions and time ticks do not write flash. BLE bonds are stored
independently from Pico's preference blob.

**Downgrade warning:** firmware predating v5 may not read the v5 state blob;
do not rely on old firmware to preserve newer preferences. Original storage
v4 layout and tests belong to the earlier Local Settings milestone; see
[OLED power](oled-power.md) for v5 acceptance.

## Sound behavior

Runtime volume travels with each bounded I2S cue job, avoiding races with the
worker. Default 25% produces the previous default PCM amplitude. The generator's
hard peak ceiling remains 2048/32768 at 100%; this is not a measured speaker-power
limiter. Start at the default when validating the small speaker.

Mute or volume zero clears pending audio; an already playing short cue may
finish. The tuner still temporarily suppresses all Pico audio without modifying
the saved user preferences. The optional fixed PWM fallback supports silence/on,
not graded volume. A build without sound hardware remains usable and silently
ignores test output. Local Settings does not initialize missing audio hardware.

`CONFIG_PICO_AUDIO_VOLUME_PERCENT` is the initial volume on a fresh audio build;
a restored record overrides it. Legacy migration uses the documented default 25.

## BLE and simulator

- `15`: open Settings (refused in car dock context).
- `25 vv`: set volume percent 0–100. Wrong length/out-of-range values are rejected.
- Existing 07 theme, 0A work duration, 22 break, 24 mute and 40 motion commands
  retain IDs/value formats; break/mute/motion now request durable saves on change.
- UUIDs, mode IDs and the 16-byte v1 status remain unchanged. The packet does not
  add readback fields for all settings; a future app view needs an additive status
  extension. Muted flag remains the explicit mute preference; volume zero does
  not rewrite it.

Simulator adds a Settings shortcut and uses the same virtual D0/D1/Back actions.
JSON includes settings index, edit key/draft/action and volume. Power-cycle tests
cover all new durable values. The optional third physical GPIO is not simulated
as an electrical circuit; the logical Back event and shared debounce class are.

## Build / flash

Use the same base profile as your current hardware. Combined mic/motion/OLED,
with the existing two buttons, from `pico_firmware` in SDK 3.4.2 terminal:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-motion -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_motion_tuner.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_tuner.conf;configs/xiao_motion.conf"
```

Optional third button added to that profile:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-settings -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_motion_tuner.overlay;overlays/xiao_third_button.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_tuner.conf;configs/xiao_motion.conf"
```

Flash the chosen build directory's `zephyr\zephyr.uf2` using the existing UF2
procedure. No mass erase or bond clearing. For sound tests use the existing
optional audio profile and wiring; the commands above do not enable I2S output.

## Validation and follow-up

Passed locally: host regression suite under C++17 with warnings as errors,
all **23 simulator tests at the original v4 milestone**, ASan/UBSan host run (LeakSanitizer disabled), asset
regeneration, Python syntax and whitespace checks. Framebuffer previews inspected.

New tests cover preview/cancel/save, hub return paths, setting bounds, timer
completion priority, metronome/tuner exit, v4 round-trip, old-record migration,
semantic validation/checksums, coalesced file saves and restart restoration.
Existing duration, Music/tuner, motion and BLE regressions still pass.

**Unverified here:** SDK target compilation, NVS migration on a physical board,
third-button electrical/debounce behavior, I2S loudness, live GUI layout and
128×64 readability in hand. No nRF toolchain or attached board is available.

On hardware, preview/cancel each option, Save several values, wait five seconds,
reboot and verify them. Test Back with two-button holds and, if fitted, D2.
Trigger a short BLE timer during an edit to check alert priority. Confirm Music,
tuner, motion off/on and bonding still operate with the saved preferences.

This completes the original Settings hub. OLED brightness/contrast and screen
sleep were subsequently implemented; see the next section. Separate
per-feature defaults, factory-reset UI and battery *hardware* policy remain
future work; no destructive reset control is added here.

## OLED follow-up

[OLED power controls](oled-power.md) extend this hub with Brightness, Screen
Time and Sleep Now after Test Sound, moving Back to the last row. Brightness
previews live and Cancel restores the saved value. Storage v5 is now the current format (see table above); the linked guide
has electrical acceptance tests. Current Settings has 11 rows including Back.

## Pocket UI follow-up

The [pocket UI](pocket-ui.md) shows one selected setting/action with larger
text. Item order, preview/Save/Cancel behavior and storage v5 remain unchanged.
