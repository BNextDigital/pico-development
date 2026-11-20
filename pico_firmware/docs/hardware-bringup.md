# Prototype hardware bring-up — current source

**Reviewed November 18, 2026.** This is a wiring and acceptance ledger for the purchased Seeed Studio XIAO nRF52840 **Sense**, not a claim of current board validation. The original OLED/BLE/D0 baseline was physically checked; later optional adapters remain to be validated.

## Decision and readiness ledger

| Component | Wiring/profile and state | Validation |
| --- | --- | --- |
| MCU | XIAO nRF52840 Sense; nRF Connect SDK 3.4.2; target `xiao_ble/nrf52840/sense` | Target confirmed; current combined build pending |
| OLED | 128×64 SSD1306 at I2C `0x3c`; 3V3/GND, SDA **D4/P0.04**, SCL **D5/P0.05** | Earlier baseline shown; brightness/sleep pending |
| D0 Action | Switch between **D0/P0.02** and GND; internal pull-up, active-low | Earlier baseline checked; new IRQ adapter pending |
| D1 Navigate | Switch between **D1/P0.03** and GND; internal pull-up | Source configured; physical validation pending |
| D2 Back (optional) | Switch between **D2/P0.28** and GND; append `xiao_third_button.overlay` | Not required or assumed installed |
| Indicator | XIAO board `led0` heartbeat | Earlier baseline checked |
| Speaker | 8 Ω / 1 W moving-coil speaker via external **MAX98357A I2S** amplifier and optional profile | Source driver implemented; I2S/electrical tests pending |
| Microphone | Sense onboard PDM mic; optional tuner overlay/config | Capture adapter implemented; target/acoustic tests pending |
| Motion | Sense onboard IMU; optional motion overlay/config | Classifier/adapter implemented; thresholds/power pending |
| Docks | Home/car devicetree aliases and software context changes | Dock electronics/ID wiring pending |
| GPS/compass, haptic | Wayfinding sensors future; no vibration motor | Not part of current board |
| Battery | USB prototype power; final user-serviceable cell/ADC policy pending | Consumption and safety design outstanding |

The D0/D1/D2 inputs are **active-low** (`GPIO_ACTIVE_LOW` with internal pull-ups): wire the contact to GND, not 3V3. GPIO edges now schedule deferred debounce/hold work instead of 20 ms idle input polling. The legacy `sw0`/`sw1` GPIO-key path on other boards is separate.

## Safe initial upload

Unplug USB before changing breadboard wiring. Do not connect a loose 8-ohm speaker, amplifier supply, dock circuit or unverified battery for the OLED-only boot. Use an nRF Connect SDK **3.4.2 terminal** in `pico_firmware/`:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao . -- -DDTC_OVERLAY_FILE=overlays/xiao_ble_ssd1306.overlay -DEXTRA_CONF_FILE=configs/xiao_ble.conf
```

Only after a successful build, double-tap reset and copy `build-xiao/zephyr/zephyr.uf2` to the XIAO BLE bootloader drive. [The first-boot guide](xiao-first-boot.md) covers serial output, pairing and display troubleshooting.

## Optional profiles and electrical precautions

- **PDM + motion:** `overlays/xiao_ble_motion_tuner.overlay` with `configs/xiao_ble.conf;configs/xiao_tuner.conf;configs/xiao_motion.conf` (OLED included).
- **D2 Back:** append `overlays/xiao_third_button.overlay` to your existing overlay list; D0/D1 hold-to-back still works.
- **I2S:** follow the dedicated [MAX98357A wiring and build instructions](sprint02.md). It uses an external amp and separately chosen pins; don't blindly combine profiles.
- **Quiet:** append `configs/xiao_quiet.conf` after initial diagnostics for meaningful power comparisons. Current draw is not yet measured.

**Never drive the moving-coil speaker directly from an nRF GPIO.** On the MAX98357A, attach the speaker across **OUT+** and **OUT−** (neither to GND). The USB/VBUS prototype amplifier supply is not an appropriate assumed battery rail. PWM `pico-buzzer` is a distinct optional fallback circuit, not an alternative direct drive for this speaker.

## Current-board acceptance checklist (pending)

1. Confirm clean boot, LED heartbeat, OLED output and a Pico BLE advertisement.
2. Verify D0/D1 tap/hold, icon navigation, minute editors and asleep-display wake-only first input; test D2 only if fitted.
3. Pair/bond from a phone and verify encrypted GATT read/write/notify and reconnect.
4. Save theme/duration/sound/motion/brightness/timeout, wait for coalesced storage and power-cycle; verify restored v5 settings and retained bond.
5. Test optional IMU pickup/rock/rest behavior and PDM tuner against known inputs.
6. Separately validate MAX98357A wiring, conservative loudness, DMA/stream recovery and lack of BLE regressions.
7. Measure power by display/audio/sensor/BLE profile with the same supply and instrumentation.

## Devicetree adapters

| Alias / chosen node | Purpose |
| --- | --- |
| `pico-focus-button`, `pico-nav-button`, `pico-back-button` | D0, D1, optional D2 |
| `sw0`, `sw1`, `led0` | Legacy buttons, onboard status LED |
| `pico-dock-home`, `pico-dock-car` | Optional dock detection inputs |
| `pico-buzzer`, `pico-i2s` | Optional PWM fallback and I2S |
| `pico-dmic`, `pico-mic-power`, `pico-imu` | Optional PDM and IMU hardware |
| `zephyr,display` | Selected OLED |

The [nRF52840 DK overlay](../overlays/nrf52840dk_ssd1306.overlay) is historical/separate hardware, not a XIAO flash target. Do not infer success on a DK or simulator proves physical XIAO behavior.

## Companion pairing recovery

See [Pair Again recovery](ble-recovery.md) for a physical-button-confirmed reset of identity 0 companion bonds. Personality, clock settings and tracking ownership are preserved.
