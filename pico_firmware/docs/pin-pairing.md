# Local passkey pairing (0.14.2-pin-pairing)

Pico now acts as a Bluetooth **Display Only** device. Zephyr generates a fresh six-digit passkey for each pairing procedure, Pico displays it, and the phone supplies the keyboard. This is authenticated LE Secure Connections, not an application password or a permanently fixed PIN.

## Flash and pair

1. Flash the new `zephyr.uf2` through the factory UF2 bootloader. Do not mass erase or reset settings.
2. For a phone previously paired with the passkey-free firmware, Forget Pico on the phone. On Pico choose **Settings → Pair Again**, tap Action and hold physical **D0 Action** at FORGET? until CLEARED. This is a one-time migration of companion bonds, not a personality/factory reset.
3. Disconnect other companion BLE clients. Choose **Settings → Pair Phone** (after Pair Again), tap Action, then hold physical **D0 Action** at READY?. Ordinary taps and remote Select cannot start the pairing window.
4. While Pico shows PAIRING, connect using Pico Companion 0.2.1 or nRF Connect; request Bond in nRF Connect if needed. Android handles the passkey dialog natively, so no new APK is required. The app's older recovery instructions do not yet mention this additional hardware step.
5. Enter the six digits shown on Pico **on the phone**, including leading zeroes. Pico shows PAIRED on success. Existing authenticated bonds subsequently reconnect without needing the local pairing window again.

The local window lasts at most two minutes. Bluetooth's own handshake timeout can end an attempt earlier. Back, holding Action again, expiry, failure, entering another utility, finding, or an alarm/timer alert closes the attempt and clears the displayed passkey. After a failure, return to Settings and reopen Pair Phone for another attempt. There is no automatic retry or fixed code such as 000000.

If the display is unavailable, the application rejects opening a new pairing window rather than using a hidden/default code. Optional peripherals and existing local utilities otherwise continue operating.

## Security and compatibility

- New pairing requires a physical hold on the confirmation screen. Advertising and already-bonded reconnections remain available outside the window; advertising alone does not grant permission for a new bond.
- Pairing callbacks accept only companion identity 0 and a single referenced connection during the local window. Callback events include a generation token so the core ignores queued results from older windows.
- The transport requests security level 4 in a locally authorized window. Private GATT values and CCCs require authentication; pairing uses Secure Connections with a 16-byte minimum key. Characteristic discovery remains available, and identity authorization does not substitute an authorization error for the standard authentication-required response.
- Old unauthenticated companion bonds must be cleared and renewed once. There is no automatic key overwrite or deletion. Pair Again clears all companion phones; it does not delete Pico personality, clock settings, the Bluetooth identity/address, or Find Hub identity 1 ownership/credentials.
- The passkey exists only in transient memory and the OLED framebuffer. No passkey log, fixed-passkey configuration, public BLE status field, persistence field, or new companion command is added. Bluetooth still persists its normal bond keys through the existing SDK settings path.
- A full event queue or security API error aborts the radio attempt; the core's existing clock bounds the UI window. No additional thread, polling loop, flash timer, or framebuffer is introduced. Cosmetic personality wake deadlines are suppressed while the passkey card is active; the OLED remains awake for this explicitly requested window.
- Timer/alarm alerts preempt pairing on the existing service tick. Tracking remains a separate background identity. Its activation/certification remains gated and unverified.

Existing mode/command IDs, the Protocol v1 status layout and persistence encodings remain unchanged. The stronger pairing requirement is an explicit bond migration, not a storage-format migration.

## Build

From `pico_firmware` in an nRF Connect SDK 3.4.2 environment:

```powershell
python tools/build_combined.py --sdk-root C:/ncs/v3.4.2 --build-dir build-pin --profile pico
```

Flash `build-pin/zephyr/zephyr.uf2`. Use `--profile combined` with a separate build directory only if retaining the optional combined measurement profile; this does not activate Find Hub.

## Tests and physical acceptance

Host tests cover physical confirmation, wake-only input, invalid/stale passkey callbacks, success/cancellation/expiry, timer preemption, volatile settings and framebuffer changes. The production BLE adapter is tested against fake Zephyr APIs for identity isolation, connection ownership/reference release, security requests, wrong-PIN failure callbacks, expired display callbacks and full-queue/low-memory failures. The simulator supports fixture events such as `event PairingPasskey 123456 TOKEN`; fixture values are not hardware-generated secrets and cannot be sent using BLE commands. The simulator reports stage/token/error and previews the PIN framebuffer; it does not model cryptography or an Android radio.

Physical acceptance remains required: verify the six-digit Android dialog, correct/incorrect entry, PIN visibility/leading zeroes, successful encrypted GATT access, disconnect/reconnect and a power-cycle reconnect. Confirm logs show `BLE link security level 4` and `BLE pairing complete; bonded=1` for a fresh bond. Also test cancelling while the phone prompt is open, timeout, timer/alarm preemption and a second phone. Record the ERROR number and unfiltered connection-time logs if pairing still fails; do not paste the passkey. The earlier generic PIN/passkey error's underlying cause has not been established by host testing.

## Linked footprint

NCS 3.4.2 / Zephyr SDK 1.0.1, full OLED/audio/microphone/motion profiles:

| Profile | Flash | Static RAM | Delta from 0.14.1 recovery |
| --- | ---: | ---: | --- |
| Pico | 332,548 B | 77,836 B | +2,648 B flash; RAM unchanged |
| Combined measurement | 397,256 B | 92,256 B | +2,132 B flash; +128 B RAM |

Both are below the existing 600 KiB flash target. The combined partition report passes; factory UF2 layout and settings partitions are unchanged. Runtime peak RAM, current draw and physical passkey interoperability have not been measured. All 42 simulator tests, the host suite and platform models pass.
