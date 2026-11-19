# Optional Pico + Find Hub measurement profile

Status: first linked measurement milestone, 2026-11-19. **Network activation is disabled.**
This image runs the Pico application and links the Nordic Fast Pair/FHN/DULT libraries.
It is not an enrolled, qualified, or certified tracker. Live tracking service orchestration,
ring arbitration and security qualification remain gates before activation.

The baseline is main `5e68f245e0d9953cc07d63469fdb2c4dc2be4bdd` (locator PR #17).
Standalone draft PR #18 remains separate; its SDK sample main/UI/drivers are not imported.
Default Pico build configuration and storage format v5 are unchanged.

## Actual linked measurements

NCS v3.4.2, Zephyr ncs-v3.4.2 (reports 4.4.2), external Zephyr SDK 1.0.1,
GNU Arm 14.3.0, no sysbuild/MCUboot. These are local ARM links, not Windows-toolchain,
physical-device, current-consumption or runtime-peak measurements.

| Build | Linked flash (bytes / KiB) | Static RAM (bytes / KiB) |
| --- | ---: | ---: |
| Pico OLED + microphone + IMU + I2S, normal optimization | 320,796 / 313.3 | 76,472 / 74.7 |
| Same Pico, LTO matching combined | 292,728 / 285.9 | 76,344 / 74.6 |
| Standalone FHN OLED + IMU | 340,160 / 332.2 | 57,780 / 56.4 |
| Standalone FHN OLED + IMU + I2S | 348,364 / 340.2 | 62,548 / 61.1 |
| Combined, all Pico peripherals, LTO | 386,892 / 377.8 | 90,764 / 88.6 |
| Combined with runtime profiling | 389,148 / 380.0 | 91,916 / 89.8 |

Combined adds **94,164 B flash and 14,420 B static RAM** against like-for-like LTO Pico.
Profiler overhead is **2,256 B flash / 1,152 B RAM**. Combined has **415,924 B** left
in its 784 KiB application region, and is **227,508 B below the 600 KiB target**.
The standalone application is not a drop-in additive component: it has different UI,
partitioning and no full Pico Core or microphone tuner. Adding standalone totals would
count common libraries/drivers twice. LTO savings are shown separately to avoid
crediting them to sharing.

The retained `.pico_fhn_size_probe` linker section holds a private activation/teardown/
reset call graph and named SDK API pointers. This prevents misleadingly small results
from dead-stripping dormant activation code under LTO. It has **no runtime caller**;
not even a debug BLE command enables it. It is not a finished activation API.
Later live integration will add callbacks and arbitration, so this is a measured
baseline rather than a final certified-product footprint guarantee.

Machine-readable measurements, ELF/UF2 hashes and load ranges:
[combined-2026-11-19.json](measurements/combined-2026-11-19.json).
`_flash_used` and `_image_ram_size` match GNU link reports. UF2 file size includes
block framing and is not application flash consumption. Static RAM does not measure
heap peaks, fragmentation, transient pressure, or stack margin.

## Partition verification and rollback

| Region | Start | Exclusive end | Size |
| --- | --- | --- | ---: |
| Existing MBR/SoftDevice reservation (read-only) | 0x00000 | 0x27000 | 156 KiB |
| Combined application | 0x27000 | 0xEB000 | 784 KiB |
| Fast Pair registration | 0xEB000 | 0xEC000 | 4 KiB |
| Existing settings + companion bonds | 0xEC000 | 0xF4000 | 32 KiB |
| Factory UF2 bootloader (read-only) | 0xF4000 | 0x100000 | 48 KiB |

Generated DTS confirms the exact addresses, the internal flash 4 KiB erase size,
and 4-byte write alignment. Compile-time assertions pin the registration and settings
regions; startup additionally checks the flash driver's actual page offset and size.
`combined_report.py` checks every flashable ELF LOAD segment and every UF2 payload
against the application region, rejects overlaps/incomplete UF2s, and enforces the
initial **700 KiB hard failure gate**. Target 600 KiB is reported separately.
No included UF2 writes registration, settings, bootloader or the low flash reservation.
No mass erase, recover, bootloader replacement or debug credentials are used.

Rollback: double-tap reset and copy the existing known-good Pico-only UF2. Its original
settings address and v5 format remain compatible. This measurement profile performs
no provisioning/reset of Find Hub ownership. A future Pico-only image near the top
of its original 788 KiB region could overwrite the reserved credential page; the
measured Pico-only image ends well below it. Validate any future rollback image's
write range before retaining registration credentials. Rollback does not enroll,
unregister or transfer ownership; those lifecycle operations remain separate.

The pinned SDK registration layout is 72 bytes: magic 4, model ID 3 plus padding 1,
private anti-spoofing key 32, SHA-256 integrity digest 32. One 4 KiB erase page fits
that record with 4,024 bytes unused. This is **not** an ownership database or a
power-fail-safe mutable credential journal. The digest detects corruption, not an
authenticated malicious replacement. Production provisioning needs an authorized
model/key, debug/readout protection and an explicit update/recovery threat model.
Two independently erasable copies would require another page or an approved storage
scheme. No credential writing/update command is shipped in this milestone.

## Shared and introduced resources

| Service | Sharing / new cost |
| --- | --- |
| Bluetooth | One `bt_enable`, host/controller and settings backend. Four connection slots, two identities and three advertising sets replace Pico's one-slot configuration. SDK reserves two connection slots. No second stack. |
| Storage | One NVS partition/backend and one boot `settings_load`. `pico/state`, existing `bt` identity 0, and SDK `fp` data remain logical namespaces. Registration is separate immutable internal flash. |
| Hardware/rendering | Existing OLED compositor/framebuffer, packed faces, GPIO, IMU, PDM microphone and I2S worker/slab. No SDK DK UI, ADXL362, fake battery, duplicate framebuffers or alternate sound worker. |
| Scheduling | Existing Pico deadline loop and Zephyr system/BT workqueues. Cooperative system workqueue required by SDK; increased system/BT stack reservations are included above. No tracker polling loop. |
| New protocol code | SDK Fast Pair, FHN, DULT, crypto, persistence and advertising manager; private sizing adapter and geometry checks. |
| Runtime diagnostics | Existing main-loop samples, thread monitoring/filled stacks, CPU counters, slab high-water counters, libc heap metrics and queue snapshots/drop counter. No new diagnostic thread. |

Selected allocations visible in the combined ELF: microphone slab 16,384 B,
audio slab 2,048 B, SoftDevice controller pool 7,286 B, SDK mbedTLS arena 512 B.
These are examples included in the total, not additional allocations to sum on top.
SDK/host radio buffers and shared libraries have interdependent sizes; the exact
whole-image delta above is more reliable than attributing every byte to one feature.

Companion isolation is active in the optional build: only identity 0 can access
Pico attributes (including CCCs), receive private notifications, or report companion
connect/disconnect events. Ordinary pairing is restricted to identity 0. SDK per-link
Fast Pair authentication remains dormant. Companion advertising uses a central slot
policy with a conservative network reserve. Runtime allocation and identity/privacy
rules during provisioned advertising are not yet qualified.

Valid Pico settings can still restore when an unrelated SDK settings namespace
returns an error; Pico's own blob validation remains required. The SDK reset hook
is scoped to identity 1; no broad `settings_delete`, all-identity unpair, or personality
reset is introduced. Network reset/removal is not exposed to users yet.

## External 2 MiB QSPI assessment

The Sense board DTS provides a P25Q16H: 16,777,216 bits = 2 MiB, with deep-power-down
support. The driver is already part of the baseline board build. No new QSPI asset
reads or credential storage are introduced. The generated source contains just
**3,054 bytes** of bitmap payload across 136 primitive arrays plus face descriptors
(the linked face table is 1,600 B; compiler deduplication can reduce payload storage).
Moving these to QSPI would save only a few KiB while adding an asset image/versioning
scheme, bounds checks, read/cache handling and flash wake latency/power. With the
current headroom, retain internal packed immutable faces. Consider QSPI only for a
larger future asset pack, with explicit sleep/DPD policy and measured wake costs.
Never place registration keys or ownership secrets there as plaintext.

## Runtime profiling and physical validation

Build `--profile profile`. Existing service wakes opportunistically log every 60 s:
thread name/address, configured stack size, minimum untouched stack bytes, CPU cycles;
audio/mic slab current and peak blocks; observed main-queue occupancy high-water and
drops; libc heap current/peak/free bytes. Queue high-water is sampled and can miss
short bursts. Thread enumeration uses Zephyr's unlocked API (threads are static here).
Libc metrics exclude the separate 512-byte mbedTLS arena and do not establish its peak
or fragmentation; crypto exhaustion and allocator failure tests remain activation gates.
Logs never print audio samples, bonds, keys or ownership data.

No hardware profile values or current readings have been collected. Profiling/USB/logging
adds power and timing overhead; use the ordinary combined profile for current testing,
and disable existing bring-up diagnostics only after the board is physically verified.
The profiler adds no separate periodic wake to the production build.

| Scenario | Required physical checks | Result |
| --- | --- | --- |
| Idle face and display timeout | animation timing, OLED actual off, IMU demand, 10+ minute stack/slab trend, current | Pending |
| Companion connected/reconnecting | bond retention, Just Works compatibility, link interruptions while navigating, status privacy | Pending |
| Finding/local ring + active timers | completion handling and sleep/wake, missing audio behavior, current | Pending |
| Music/tuner + BLE writes + OLED redraw | peak stacks/heap/slabs, queue drops, audio deadlines, mic power stop/resume, 30+ minute soak | Pending |
| Network ring/DULT + Music/timer | priority arbitration, SDK timeout/physical acknowledgement, required acoustic output, mic suspension/resume | Blocked: live routing and authorized activation |
| Sleep/wake/power loss | actual OLED/IMU/PDM/I2S currents, amplifier idle/SD wiring, USB disconnected idle current, reboot persistence | Pending |
| Interrupted NVS/registration writes | real power-cut page recovery and wear/capacity with SDK ownership + multiple bonds | Pending |

Current measurements should record battery voltage, board revision, OLED brightness,
USB state, speaker/amp wiring, radio intervals and meter/sample rate. Capture average
and peaks for idle, connected, local finding, network ringing, Music and sleep. No
claimed battery-life or low-power certification follows from these links.

## Regression results and remaining activation gates

Passed: existing portable core suite, Nordic integer-limit check, GPIO model, OLED
present/absent models, audio reference model, **33 simulator tests**, new identity/slot
and alert-priority contract tests, five image/layout/budget fault tests, and real
Storage adapter model tests for namespace failure, truncated/corrupt reads, `-ENOMEM`
write retry, newer write during failure, and reload of the durable snapshot.

These storage tests exercise the adapter with a fake backend; they do not simulate
NVS page metadata/erase atomicity. No live BLE interruption, simultaneous network
traffic, crypto memory exhaustion or physical power-cut test has passed yet.

Existing features disabled: **none**. Modified only in the optional profile: LTO,
Bluetooth capacity/identity guards, cooperative workqueue sizing, guarded namespace
load handling, app end address. Optional profiler is a separate configuration.
Deferred: enrollment/discoverability UI; credential provisioning; full central radio
policy under provisioning/ownership transitions; GAP/DIS privacy during provisioned
network operation; shared ring/audio cancellation and motion demand fanout; network
reset/recovery state machine; production security/acoustic/power qualification.

`tracking_policy.hpp` codifies DULT > timer completion > owner network ring > local
finding > ordinary feature audio and microphone/motion arbitration rules. Alert helpers
are contract-tested groundwork, **not connected to live SDK callbacks yet**. The sizing
adapter explicitly fails ring requests and treats unavailable motion conservatively;
those callbacks are unreachable in ordinary operation and cannot justify activation.
Do not enable the retained probe by calling it. Replace it with a reviewed service
lifecycle and latest-state latch (critical stops/safety cannot depend on a fallible UI
queue), test it, then remove the sizing anchor when real reachability retains SDK code.

## Build and flashing

From an NCS 3.4.2 terminal with its toolchain/west environment, in the repository:

```powershell
python pico_firmware/tools/build_combined.py --sdk-root C:/ncs/v3.4.2 --build-dir pico_firmware/build-combined --profile combined
```

Use the actual SDK root installed on the machine. On Windows, if the NCS terminal
already selects its bundled toolchain, preserve that environment; this helper passes
`NCS_TOOLCHAIN_VERSION=NONE` to use it rather than selecting another toolchain.
For diagnostics use a new directory and `--profile profile`; normal full Pico is
`--profile pico`, and the fair LTO comparison is `--profile pico-lto`. The helper runs
west from the SDK root, so application-directory west discovery is not required.
All profiles enable the existing OLED, PDM, IMU and optional MAX98357 wiring (D8/D9/D10).
If the amplifier is physically absent, verify graceful audio-not-ready behavior; do
not describe network audible locating as available.

The helper runs the guarded post-link report and fails on unsafe layout/write ranges
or 700 KiB. Review `pico-resource-report.json` before flashing. Double-tap reset for
UF2 bootloader mode; copy only the verified `build-combined/zephyr/zephyr.uf2` to the
XIAO drive. Do not use a DK binary, merged provisioning HEX or `west flash --erase`.
Keep the current Pico-only UF2 for rollback. No real/debug Google key is needed to
boot this measurement image; it does not advertise/register as a Find Hub tracker.
