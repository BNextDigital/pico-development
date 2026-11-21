# Find Hub prototype validation

Date: November 19, 2026. All results below are compilation/host verification;
no connected XIAO, phone/network provisioning or acoustic/power test ran here.

## Build environment

- Unmodified nRF Connect SDK `v3.4.2` and its pinned `sdk-zephyr` `ncs-v3.4.2`
  (Zephyr reports 4.4.2).
- External Zephyr SDK 1.0.1, GNU ARM compiler 14.3.0, Linux x86_64, Python 3.12.
- No sysbuild, no MCUboot, `NCS_TOOLCHAIN_VERSION=NONE` for the external toolchain.
- This is not a validation of the bundled Windows Nordic toolchain or physical
  UF2 loader. The source/version interfaces are pinned to the user's SDK.

## Successful target builds

| Image/profile | Board | Flash used | RAM used | Flash region |
| --- | --- | --- | --- | --- |
| Unmodified SDK locator-tag sample | nRF52840 DK | 280,276 B | 42,812 B | 496 KiB |
| Isolated Pico FHN, OLED + Sense IMU, build-only | XIAO Sense | 340,160 B | 57,780 B | 740 KiB |
| Isolated Pico FHN, OLED + Sense IMU + MAX98357 I2S | XIAO Sense | 348,364 B | 62,548 B | 740 KiB |
| Existing Pico companion, combined OLED + mic + motion | XIAO Sense | 312,408 B | 71,576 B | 788 KiB |

The SDK locator sample was built for its supported DK, **not flashed**. The
ordinary companion's successful target build validates compilation of the
merged local-locator revision; it does not validate its Android end-to-end path.
These are separate applications. Their totals cannot simply be added or
subtracted to predict a future combined image. RAM figures are static linker
allocation, not measured worst-case stacks, heap headroom or runtime endurance.

The XIAO no-speaker build is also checked as a build-only profile; it cannot be
packaged by the helper for network testing. Both profiles have no credentials
by default and intentionally produce no UF2. Firmware activation without model
registration data is not expected to succeed.

The SDK emits its expected warning that DULT is experimental. Temporary setup
failures were resolved: old external SDK version, missing Python modules,
prototype board-name check, no-prompt volume assignment, partition labels and
version-dependent banner configuration. Final target results apply to the
corrected configuration. No warnings were suppressed to force success.

## Host checks

- Prototype portable guards are included in the normal host check entry point.
  Seven Python tests pass, including registration byte-for-byte parity with
  Nordic's pinned provisioning tool using synthetic test-only data.
- UF2 tests reject forbidden ranges, wrong family, invalid sequence/size,
  oversized application, malformed model/key records and bad integrity hash.
- Generated DTS/config guards reject wrong board, code offset/size and settings
  partitions that could reach ordinary Pico's storage or bootloader. The audio
  profile's generated DTS also parses with DTC 1.7.0; it reports the SDK's usual
  Nordic overlapping peripheral-address warnings.
- C++17 button tests pass with warnings as errors: debounce/bounce, tap vs hold,
  ringing acknowledgement, five-second two-button reset, release suppression
  and millisecond rollover.
- Existing portable core/platform regression suite and all 33 simulator tests
  pass. This isolated addition does not modify their implementation.
- Generated images, private provisioning data and SDK/toolchain files are not
  committed. No real model/private key or credential-bearing UF2 was created.

## Outstanding hardware/network gates

Authorized model credentials and test-account access; actual UF2 flash/restore;
normal settings and phone bond/cache behavior; OLED/GPIO/IMU operation; I2S
wiring, cancellation latency, output level and requested-volume handling; real
battery reporting; separated-motion/DULT behavior; Fast Pair/FHN provisioning,
clock recovery, encrypted crowd reports, unprovisioning/reset and current draw.
Official network approval/certification is not established.

## Recheck after synchronizing with Pico 0.14.2

Both standalone XIAO Sense profiles linked locally with SDK 3.4.2 and Zephyr
SDK 1.0.1. The build helper verified the generated configuration and devicetree
partition layout.

| Standalone profile | Flash used | Static RAM used |
| --- | ---: | ---: |
| OLED + Sense IMU | 344,024 B | 57,780 B |
| OLED + Sense IMU + MAX98357 I2S | 351,756 B | 62,564 B |

The host suite, all 42 simulator tests, and the standalone checks including
registration format parity with the pinned Nordic tool passed. Hardware
operation, network provisioning, and certification still require separate
validation.
