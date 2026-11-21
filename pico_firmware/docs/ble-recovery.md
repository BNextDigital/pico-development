# Companion pairing recovery

Firmware `0.14.1-bond-recovery` adds **Settings → Pair Again**. This is a recovery tool for possible stale companion bonds; an Android “incorrect PIN or passkey” message alone does not establish the cause.

## On the device and phone

1. Close the companion app and disconnect other BLE clients. Forget/remove Pico in the phone's Bluetooth settings and saved nRF Connect devices, if listed.
2. Open Pico's Settings, navigate to **Pair Again** (after Back), and tap Action to open **FORGET?**.
3. Hold the physical **D0 Action** button until **CLEARED** appears. A tap or a remote Select command cannot confirm deletion. If asleep, the first input only wakes Pico.
4. In nRF Connect, start a fresh scan, connect to Pico, then request Bond. Accept the phone's pairing prompt. Do not enter a guessed fixed PIN.
5. If pairing still fails, capture the unfiltered serial log from reset through the attempt, including the recovery result and saved-bond count. A zero count before reset points away from stale device-side bonds.

**FAILED** means the reset API returned an error. Return to Settings to retry. Hold Action or use Back to leave the result screen.

## Scope and security

The adapter disconnects companion connections and calls `bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY)`. Only identity 0's companion bonds and GATT client configuration are targeted. All previously bonded companion phones must bond again. The Bluetooth identity/address remains stable. There is no mass erase, factory reset, identity reset, or deletion of personality, clock, or Find Hub identity 1 data.

Find Hub activation is still a separate, gated milestone. This recovery does not change ownership or provisioning. Existing encrypted GATT permissions and Secure Connections policy remain intact.

SMP logging is reduced from Debug to Info to avoid debug key material and noisy bursts; the log buffer is increased by 1 KiB to 2 KiB. OLED diagnostics remain enabled. Logs report identity 0's bond count before reset and the reset result.

The same patch also corrects forwarding of the command Event's auxiliary payload into the core, required for clock/timezone and alarm commands. That transport bug does not explain pairing rejection.

## Build and validation

From a terminal already inside `pico_firmware`:

```powershell
python tools/build_combined.py --sdk-root C:/ncs/v3.4.2 --build-dir build-bond-recovery --profile pico
```

Flash `build-bond-recovery/zephyr/zephyr.uf2` using the factory UF2 bootloader. Do not erase settings. To retain the optional combined measurement profile, substitute `--profile combined` and a different build directory; network activation remains disabled.

Host tests cover explicit confirmation, wake-only behavior, failed/successful results and durable-settings preservation. Simulator tests model the recovery effect without a radio. Production BLE-adapter tests with fake Zephyr APIs cover identity isolation, disconnect/unpair failure, full Event forwarding, queue rejection and GATT identity authorization. Both XIAO profiles must link before delivery.

Physical phone rebonding, stored-key deletion across power loss, live identity concurrency and speaker behavior remain unverified. The separate silent-speaker issue remains open. Recovery tests do not qualify physical BLE or flash durability.

## Passkey firmware migration

Firmware 0.14.2 requires authenticated Secure Connections for private Pico GATT. After clearing old companion bonds, open **Settings → Pair Phone** with a physical Action-button hold, then enter the OLED passkey on the phone. See [PIN pairing](pin-pairing.md) for the two-minute window, recovery and hardware acceptance steps. Command IDs, status bytes and durable settings schemas are unchanged.
