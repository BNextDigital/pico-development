# OLED brightness and screen sleep

Menu -> Settings now includes Brightness, Screen Time and Sleep Now. Existing
items keep their order through Test Sound; Back moves after the new controls.
Navigate chooses an editor action; Action applies it. Back/hold cancels.

| Control | Behavior |
| --- | --- |
| Brightness | 5–100% in five-point steps; live preview, Save or Cancel |
| Screen Time | Off, or 1–30 minutes in one-minute steps; Save or Cancel |
| Sleep Now | Immediately blank the screen and return to Home |

Brightness defaults to 50% (contrast 128), timeout to Off. Percent is a relative
controller contrast setting, not calibrated luminance or a percentage of power.
Five percent remains selectable without making brightness zero an accidental
lockout; explicit sleep is the way to turn the screen off.

The first physical-style button input while asleep wakes only: it does not
select a menu row, start/cancel a timer, or increment interactions. The next
input performs its usual action. These shared logical input events have the
same behavior when injected via BLE/simulator. Explicit navigation/configuration
commands that normally register activity also wake normally. BLE connection,
status/battery updates and diagnostic probes do not wake the display. Existing
eligible motion pickup/rock reactions can wake it; motion rest does not.

Auto-sleep uses input inactivity independently of personality Sleepy mood.
It may blank a running/paused timer, menu or Settings hub. It is inhibited while
editing settings/durations, tuning, running the metronome, or displaying a timer
completion. Manual sleep is also refused during tuner/metronome/completion.
Timer completion always wakes the screen and renders the existing alert before
normal return-home behavior. Timers and BLE keep operating with the OLED asleep.

## Implementation

Portable State contains durable brightness/timeout and transient display_asleep.
Core activity wakes/restarts inactivity timing, while animation service applies
the timeout. The deadline query includes timeout expiry; asleep screens omit
unneeded facial deadlines while retaining the existing one-second service tick.
No sleep/wake transition requests a flash write.

The display compositor outputs black for simulated sleep. The platform adapter
uses Zephyr display_set_contrast (linear mapping from 5–100% to 13–255) and
display_blanking_on/off. Contrast commands are sent only on change, including
preview/cancel. Blanked displays skip frame composition/writes. Wake restores
the current framebuffer while output is blanked, then enables output.

Driver API references: [contrast and blanking](https://docs.zephyrproject.org/latest/doxygen/html/group__display__interface__backend.html).
No direct SSD1306 I2C commands, PWM dimming or supply switching are introduced.
Controller blanking does not disconnect OLED module power and is not zero-current
shutdown. Actual SDK driver support, brightness and current need bench validation.

If contrast fails, the adapter logs once and keeps the driver's setting until
restart. If blanking fails, it logs once and falls back to a black framebuffer
until restart. A wake/write failure remains retryable. Missing display hardware
remains nonfatal. Diagnostics retain their existing probes and now report the
requested display_asleep state; frame_written is the existing render-success
indicator, not a count of physical writes. Diagnostics can still generate bus
traffic while blanked.

## Persistence / simulator

Storage v5 is 32 bytes. Bytes 0–22 retain v4 meaning, byte 23 holds brightness,
bytes 24–27 hold timeout minutes as little-endian uint32, bytes 28–31 hold the
FNV-1a checksum of bytes 0–27. The decoder accepts v1/v2 (16), v3 (20), v4 (28)
and v5 (32). The NVS loader also accepts v4's length. Old settings migrate with
50% brightness and auto-sleep Off. Sleep state is never persisted; boot is awake.
Confirmed changes use existing coalesced saves. Older firmware cannot decode
v5 records; forward migration does not provide reverse migration.

BLE command IDs, UUIDs and the 16-byte status packet are unchanged. These are
local settings; new BLE write IDs/readback extensions are not introduced.
Simulator event injection adds SetBrightness, SetDisplayTimeout and SleepDisplay.
JSON exposes display_brightness_percent, display_preview_percent,
display_timeout_minutes and display_asleep. The GUI previews relative brightness
with illustrative pixel colors and black sleep frames, not a luminance model.

## Validation and physical acceptance

Local checks pass: the host suite, enabled/absent display adapter models and
all 27 simulator tests. ASan/UBSan runs pass (leak detection disabled), as do
asset regeneration, Python syntax and whitespace checks. Rendered previews were
inspected; the live desktop GUI and physical OLED were not tested here.

Run `bash tests/host/run.sh` and `bash simulator/run-tests.sh`. Host tests cover
preview/cancel/save, bounds, v4 migration and v5 corruption, persistence, sleep
expiry/clock wrap, wake consumption, motion wake, background timer alerts,
protected editing and blank rendering. Production display adapter model tests
cover enabled/absent display, contrast caching, black-frame fallback, repeated
sleep, frame-before-unblank ordering and write/wake recovery. Those model tests
are not a Zephyr build or electrical validation.

Use your existing nRF Connect SDK 3.4.2 terminal and profile. Combined OLED,
microphone and motion, from pico_firmware:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao-motion -- "-DDTC_OVERLAY_FILE=overlays/xiao_ble_motion_tuner.overlay" "-DEXTRA_CONF_FILE=configs/xiao_ble.conf;configs/xiao_tuner.conf;configs/xiao_motion.conf"
```

Keep any existing third-button/audio overlay additions. Flash
build-xiao-motion/zephyr/zephyr.uf2 using the established bootloader procedure.
No new wiring, mass erase or bond clearing is required.

1. Preview 5%, 50%, 100%; verify readability and Cancel restoration. Save a new
   brightness, wait five seconds and reboot; check settings and BLE bonds.
2. Set Screen Time to one minute. Check blanking and that the first button only
   wakes; test Action, Navigate and optional Back independently. Repeat with Off.
3. Run a local timer, then choose Sleep Now. Confirm completion wakes into the
   alert and sound remains optional. Check pause/resume while navigating.
4. Leave a brightness/duration preview open beyond the timeout. It must remain
   visible. Trigger a completion during preview; saved brightness must return.
5. Repeat tuner/metronome and motion wake checks; inspect for contrast/blanking
   fallback warnings. Repeat sleep/wake with BLE connected and disconnected.
6. Measure otherwise identical awake/low-brightness/blanked configurations with
   diagnostics disabled before claiming savings or estimating battery runtime.

Target compilation, actual contrast/blanking behavior, NVS migration on the
board, current draw and OLED readability remain unverified in this environment.
