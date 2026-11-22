# Clock, alarms and stopwatch (0.14.0)

Clock is an everyday utility in the existing icon menu, not a new mode ID or a
separate application loop. It runs with timers, Focus and Music. Existing mode IDs,
BLE commands, the 16-byte status packet and the 32-byte `pico/state` v5 record remain
compatible. The optional combined Find Hub profile retains its activation gate.

## Controls and display

Root menu: navigate to **CLOCK**, then Action. Existing nine root positions remain
unchanged; Clock is appended at index 9. The new screens use the existing compositor,
large time digits and one selected action. No extra framebuffer or graphical subsystem.

* Clock: local HH:MM, weekday and month/day. AM/PM appears in 12-hour mode. Actions:
  Alarms, Stopwatch, Set Time, Options, Home.
* Alarms: four fixed slots, selected one at a time. Action opens its editor. Fields:
  hour, minute, recurrence, enabled. Navigate selects Decrease, Increase, Next Field,
  Save or Cancel; Action applies it. Supported physical presets are Once, Daily,
  Weekdays, Weekend and each individual weekday. BLE supports any weekday bitmask.
* Stopwatch: Start/Pause/Resume, Reset, Back. Start starts a fresh run; Pause preserves
  elapsed time; Resume continues it; Reset stops and clears it. Pausing also serves as
  Stop. Whole seconds are shown, with hours when necessary; fractional milliseconds
  are retained across pause/resume. No laps or stopwatch persistence in this version.
* Set Time: edit year/month/day/hour/minute/UTC offset, using the same five actions.
  Save sets seconds to zero; Cancel/Back discards the draft. Offset changes in
  15-minute steps, bounded to ±14 hours. Invalid/out-of-range dates cannot be saved.
* Options: 12/24-hour format, optional face glance, optional USB desk display.
* Back/hold returns to the parent screen. Existing wake-only first-button behaviour
  is preserved. During an alarm, a button dismisses the alarm without activating
  the obscured action or petting Pico.

The face glance uses the existing bottom status area only when no timer/Focus status
occupies it. USB desk mode gives the Home face a simple clock presentation (a pet
reaction still briefly shows Pico), and keeps Home/Clock visible only while VBUS is
detected. Unplugging restores the normal display timeout. Explicit Sleep Now still
works. The Nordic POWER VBUS status is read on the existing second service pass;
no GPIO, new peripheral, worker or USB enumeration dependency is added. Other boards
without the nRF52840 adapter conservatively report no USB power.

## Timekeeping and corrections

The portable engine anchors UTC to monotonic uptime. Valid UTC/local dates are
2000–2099. It stores a signed offset in minutes: **local = UTC + offset**. It does
not contain a timezone database. A phone/manual offset update handles travel and DST;
Pico cannot predict a future DST transition while offline without such an update.
Minor drift is accepted. There is no NTP, GPS, external RTC or calibration routine.

The XIAO Sense build already selects `CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL=y` at
32,768 Hz. The clock reuses Zephyr uptime from that existing LF/RTC timing source.
No second RTC driver/counter or clock-source change is introduced. Supported System ON
idle sleep retains uptime. System OFF/reset or power loss resets the application:
**time is unset until a fresh phone/manual setting**. Saving a timestamp would not
establish elapsed unpowered time, so UTC and running stopwatch/alarm alerts are never
restored as valid. Saved format, offset and alarm definitions survive normal reboot.

Clock corrections affect wall time and alarm scheduling only. Stopwatch and existing
countdowns continue on monotonic time. The stopwatch uses existing millisecond service
samples and carries its fractional remainder across pauses; the display updates only
when its whole-second value changes. Wrap handling covers the ordinary uint32-ms
rollover while the existing firmware continues servicing clocks.

The new time-sync command is one atomic event containing UTC and offset. Internal
Event size grows from 8 to 12 bytes (the existing queue keeps its same bounded depth).
Queue saturation rejects the entire command, without applying half a timezone/time
transaction. This internal change does not alter the BLE wire layout of older commands.

## Alarm rules and shared resources

* Four allocation-free slots, local minute-of-day, weekdays bitmask (Monday bit 0),
  enabled flag. A zero mask means one shot at the next occurrence of that local time.
* One-shot alarms resolve and persist their target local date when time becomes valid.
  Reboot/resync does not silently move an expired one-shot to the following day.
  If no supported future date exists at the end of 2099, that one-shot is disabled.
* Repeating alarms schedule their next selected local weekday. The latest fired local
  day is remembered in RAM to avoid replaying the same day's alarm after a backward
  correction such as the DST repeated hour.
* Explicit clock/timezone corrections schedule future occurrences; skipped historical
  alarms do not ring retroactively. Ordinary service lateness has a 120-second grace.
  Larger late gaps skip the occurrence and disable an expired one-shot.
* Due slots share a ringing bitmask and a single audio alert, not multiple queues.
  Each alert lasts at most 60 monotonic seconds and repeats a short existing completion
  cue about every three seconds. All buttons or command 0x63 dismiss it. No snooze
  is supported; dismissal finishes that occurrence.
* Intentionally enabled alarms override ordinary UI mute/zero-volume with at least
  25% configured cue volume. Disable a slot to silence it. With absent/failed audio,
  the visual alarm remains available and says NO SOUND; no audible success is claimed.
* The existing audio worker owns the speaker. Owner changes invalidate both queued
  and in-flight stale jobs. Microphone capture is suspended during a clock alarm and
  restarted with a fresh tuner generation afterward; the selected tuner/metronome
  state is not erased by the clock alarm. Lower-priority beat/finding chirps are
  suppressed while the alarm owns output. Stopwatch and countdowns keep running.
* An already-pending timer completion has presentation/audio precedence over a clock
  alarm. The clock alarm stays independently pending in its bounded alert window;
  dismissing it does not clear the timer completion. The shared tracking priority
  contract remains DULT > timed alerts (timer/alarm) > owner network ring > local
  finding > feature audio. **Live FHN/DULT routing is still gated in the baseline**;
  this feature does not claim or activate network alert integration.

Time/date render at minute boundaries on visible Clock/glance/desk screens. Stopwatch
renders at whole-second changes only while selected. A sleeping OLED does not receive
cosmetic clock rendering. The existing scheduler's second pass performs clock/alarm
checks; no new deadline loop, task, periodic BLE request or cosmetic wake is added.
The BLE read cache refreshes there without notifications, so long stopwatch runs do
not depend on an old 32-bit-ms connection anchor. Ordinary time progression does not
write flash. Daily resync with unchanged settings also does not write flash.

The clock starts no idle microphone/I2S work. An alarm temporarily needs the existing
speaker and stops PDM capture. Actual amplifier quiescent/shutdown current depends on
existing hardware wiring (there is no newly invented SD pin); measure it physically.

## Persistence

`pico_clock/config` is a separate, versioned **44-byte** record in the existing NVS
backend: magic `PCLK`, version 1, option bits, signed offset, four 8-byte alarm records,
and FNV-1a integrity checksum. One-shot target day is part of each alarm record. No UTC,
stopwatch, ring state, BLE identity or ownership credentials are stored in this key.
Absent records on upgrade use defaults; old `pico/state` versions/migrations are unchanged.
Invalid/truncated clock records affect clock defaults only. A downgrade can ignore this
new namespace. Network identity reset is not a clock/personality factory reset.

Storage uses the existing coalescing work item and mutex for both keys. An ordinary
edit is durable after its five-second coalescing delay. A power cut before that may
lose the edit. Failed writes retry without overwriting a newer pending snapshot;
an older retry must not postpone a newly queued save. NVS page-level recovery still
requires real power-cut testing, beyond host fake-backend coverage.

## Companion integration contract

The firmware/protocol and simulated phone-packet path are implemented here. The
production Flutter companion app has **not** been changed in this firmware PR;
its transport/UI must adopt this contract before automatic phone sync and app alarm
editing work end to end. Simulator packet tests do not establish a successful physical
Android/iOS pairing/time-sync test.

Use encrypted/bonded identity-0 Pico service access. Clock capability is bit 9 in the
unchanged v1 status. New characteristics use the same UUID suffix as existing service:

| Characteristic | UUID prefix | Operations |
| --- | --- | --- |
| Clock status | 8e7b0005 | encrypted Read + Notify, 20 bytes |
| Clock settings/alarms | 8e7b0006 | encrypted Read, 44 bytes (long read supported) |
| Commands (existing) | 8e7b0003 | encrypted write; new commands below |

App sequence:

1. After service discovery and pairing, subscribe to clock status and read it. Do not
   rely on the connection-time notification reaching an unsubscribed client.
2. If time is invalid or sync-due is set, send current Unix UTC seconds plus the phone's
   **current** offset. Use UTC epoch, not a locally shifted timestamp. Verify fresh
   status/notification after the accepted write.
3. While connected, react to the sync-due transition at approximately 24 hours. Also
   check on app foreground/reconnection; a lost notification must not prevent resync.
   Use a bounded retry after a failed write, not continuous polling.
4. On material timezone/DST changes, send UTC + new offset immediately. On mobile
   resume, compare the platform offset with the cached offset and correct if needed.
5. Offline, send nothing. Pico maintains its clock and recurrence locally. A later
   reconnect reads status and refreshes only when needed.
6. Older firmware lacking capability/characteristics must continue with its existing
   protocol. All old command IDs and the legacy status length remain valid.

Pico Companion 0.3.0 uses this flow. It obtains the full settings value with a
native characteristic read and verifies the length and checksum. The platform
handles ATT Read Blob continuation, including a 23-byte MTU. The app does not
request an MTU before bonding. The firmware adapter regression models reads at
offsets 0, 22 and 44. Actual Android/XIAO radio validation remains a separate gate.

Clock status bytes (little-endian):

| Byte(s) | Meaning |
| --- | --- |
| 0 | Clock schema version 1 |
| 1 | flags: bit0 time valid, bit1 sync due, bit2 24h, bit3 glance, bit4 USB desk, bit5 USB VBUS, bit6 stopwatch running, bit7 alarm ringing |
| 2–5 | UTC seconds, zero if invalid |
| 6–7 | signed UTC offset minutes |
| 8–11 | elapsed stopwatch whole seconds |
| 12 | ringing alarm bitmask, bit0 = slot 0 |
| 13 | enabled alarm bitmask |
| 14–17 | monotonic seconds since most recent time setting |
| 18–19 | stopwatch fractional remainder milliseconds, 0–999 |

Notifications happen on sync/configuration, alarm and stopwatch control changes,
connection/status requests and stale-time transition. They are not emitted every second
or minute. Reads compute a current snapshot from the latest monotonic cache anchor.
Normal GATT service-changed support remains enabled; clients should rediscover services
when upgrading firmware. New attributes are appended after the old Pico attributes.

| Command | Payload after command byte |
| --- | --- |
| 0x60 SyncTime | u32 UTC seconds + i16 signed offset minutes (7 bytes total) |
| 0x61 SetClockOptions | u8 mask: bit0 24h, bit1 glance, bit2 USB desk (2 bytes total) |
| 0x62 ConfigureAlarm | u8 slot 0–3 + u16 minute 0–1439 + u8 weekdays 0–127 + u8 enabled 0/1 (6 bytes total) |
| 0x63 DismissAlarm | none |
| 0x64 StopwatchStart | none; starts fresh |
| 0x65 StopwatchPause | none; Stop/Pause |
| 0x66 StopwatchResume | none |
| 0x67 StopwatchReset | none; stops and clears |
| 0x68 OpenClock | none |

Clock configuration read layout: bytes 0–3 ASCII PCLK; 4 version=1; 5 option bits;
6–7 signed offset; bytes 8–39 four records of `{u16 minute, u8 weekdays, u8 enabled,
i32 once_day}`; bytes 40–43 FNV-1a over bytes 0–39. `once_day=-1` is unresolved; other
values are local civil days since Unix epoch. Clients configure through commands,
not by writing the checksum-bearing persistence blob.

Manual bring-up examples, from the repository in an NCS terminal:

```powershell
python pico_firmware/tools/clock_packet.py sync
python pico_firmware/tools/clock_packet.py alarm --slot 0 --time 07:00 --repeat weekdays
python pico_firmware/tools/clock_packet.py options --12-hour --glance --usb-desk
```

These print hex only. Write it with response to encrypted characteristic 0003 using
nRF Connect, then read 0005. For example `62 00 a4 01 1f 01` enables slot 0 at 07:00
Monday–Friday. `63` dismisses it; `64`, `65`, `66`, `67` control the stopwatch.
The simulator also supports `event SyncTime UTC OFFSET_PLUS_840`, raw `ble HEX`, and
GUI Sync Phone Time/USB-power controls. Use manual editing for fully offline operation.

## Verification and limits

Host coverage: all 2000–2099 date round trips, leap/year/month rollover, midnight/noon
formats, invalid input, UTC corrections with independent stopwatch/countdown, partial-
second pause accounting, recurrence/weekday masks, offset changes, same-day replay
suppression, one-shot expiry, grace/timeout, corruption/truncation, reboot invalidity,
physical control routing, rendering bounds, sleep/USB policy and packet validation.
Storage models cover both namespaces and failed writes; audio models cover queue purge
and in-flight cancellation on alarm/timer ownership changes.

Simulator coverage includes seven new end-to-end scenarios using the production core:
phone packets/offline/daily refresh, recurrence and physical acknowledgement, settings
restart, interrupted unsaved edits/corrupt record, minute rendering/sleep/USB desk,
manual time/alarm controls, and DST correction while stopwatch/countdown continue.
Existing scenarios remain in the suite. See the resource report for build/test results.

Physical checks outstanding:

* Actual app adoption and bonded phone sync/read/notify, reconnect, DST/timezone change.
* Alarm acoustics/volume, absent speaker, audio DMA cancellation, mic restart and
  simultaneous timer/locator/clock alert timing on real hardware.
* Time and stopwatch progression through System ON sleep, USB unplug/replug, reset
  and power loss; verify actual POWER VBUS status on this board.
* OLED timeout/manual sleep and USB desk behaviour; power-cut NVS recovery and retained
  companion bonds/personality.
* Average/peak current and runtime stack/heap/slab/queue profiles with clock hidden,
  selected, asleep, stopwatch running, alarm ringing and BLE connected.

No measured idle-current or battery-life claim is made from host/simulator tests.
No new clock thread, framebuffer, radio loop, external hardware, timezone database,
quiet-hours engine, snooze, lap history or internet time service is introduced.

## Measured delivery

NCS 3.4.2, Zephyr ncs-v3.4.2 (4.4.2), external Zephyr SDK 1.0.1 / GNU Arm 14.3.0,
XIAO Sense with OLED + IMU + PDM + I2S. Baseline is the merged #19 measurements;
combined profiles use the same LTO configuration and preserve the retained SDK sizing
anchor. All three final ARM targets linked and passed the existing ELF/UF2/budget gates.

| Profile | Baseline flash / RAM bytes | Clock flash / RAM bytes | Increment flash / RAM bytes |
| --- | ---: | ---: | ---: |
| Full Pico (normal optimization) | 320,796 / 76,472 | 330,900 / 76,812 | +10,104 / +340 |
| Combined (LTO) | 386,892 / 90,764 | 396,820 / 91,104 | +9,928 / +340 |
| Combined + profiling | 389,148 / 91,916 | 399,708 / 92,128 | +10,560 / +212 |

Combined Clock is **387.5 KiB flash / 89.0 KiB static RAM**, leaving 405,996 bytes in
the 784 KiB application region. The different profiler RAM delta includes linker
alignment effects; these are measured whole-image totals, not theoretical field sums.
Static RAM still does not establish runtime peak/fragmentation or current consumption.
No extra dynamic allocation, task or display buffer was added by the portable clock.
The internal Event queue, fixed alarm state, settings snapshots and GATT read cache
account for the small static increase. All original features and face assets remain.

[Machine-readable comparison, image hashes and write ranges](measurements/clock-2026-11-19.json).
Host/model and 40 simulator tests pass. Clock host tests also passed AddressSanitizer
and UndefinedBehaviorSanitizer; LeakSanitizer could not run in the ptrace-based environment,
so leak scanning was disabled for that rerun. Physical phone, sleep, acoustic, runtime
peak/current and NVS power-cut validation remain outstanding.

Build from the repository in the installed NCS 3.4.2 terminal:

```powershell
python pico_firmware/tools/build_combined.py --sdk-root C:/ncs/v3.4.2 --build-dir pico_firmware/build-clock --profile pico
```

Use `--profile combined` with a separate build directory for the optional combined image,
or `--profile profile` for runtime diagnostics. Substitute the actual local SDK path.
The helper performs no flashing/provisioning. Confirm its post-link report, double-tap
reset, and copy only the verified `zephyr/zephyr.uf2` to the XIAO drive. Never mass-erase
or substitute a DK/provisioning image. Existing application/settings/credential/bootloader
boundaries are unchanged. Keep a known-good UF2 for rollback; the added clock namespace
does not change or clear existing personality settings or companion bonds.
