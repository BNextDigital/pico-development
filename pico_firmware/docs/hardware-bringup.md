# Prototype hardware bring-up

For the current Sprint 01 build, wiring, compatibility and acceptance steps,
see [Sprint 01](sprint01.md). The historical DK example below is a separate board.

## Decision ledger

| Area | Current prototype decision | Confidence |
| --- | --- | --- |
| MCU/radio | Nordic nRF52 family using nRF Connect SDK and BLE | Confirmed |
| Development board | Seeed Studio XIAO nRF52840 Sense; target `xiao_ble/nrf52840/sense` | Confirmed |
| Display | Small monochrome OLED with layered bitmap components | Confirmed concept |
| Display controller | 128x64 SSD1306 at `0x3c`, SDA D4 / SCL D5; baseline hardware rendered successfully | Confirmed baseline |
| Input | D0 tap/hold Focus controls; optional D1 menu controls in Sprint 01 | D0 physically confirmed on baseline; D1 pending |
| Indicators | XIAO onboard red LED for connection/heartbeat | Confirmed baseline |
| Audio output | 8-ohm/1 W speaker + MAX98357 I2S breakout; PWM adapter is only an optional alternative | I2S driver pending |
| Audio input | XIAO Sense onboard PDM microphone for future tuner/notes | Hardware known; capture adapter pending |
| Haptics | No vibration motor | Explicitly excluded |
| Docks | Home and car context; GPIO aliases supported now | Confirmed behavior, electrical ID method pending |
| GPS/compass | Wayfinding extension | Future optional hardware |
| Fitness sensing | None | Explicitly out of scope |
| Power | USB/dev-board power for first bring-up; final user-serviceable battery circuit pending | Prototype-safe default |

## Safe first boot

1. Flash the firmware to the development board with no loose speaker connected.
2. Confirm a boot log, a slow heartbeat on the board's `led0`, and a `Pico` BLE
   advertisement.
3. Press the board's `sw0`; the interaction counter should advance. `sw1`, when
   present, cycles modes.
4. Pair from a BLE utility, subscribe to the status characteristic, and send
   command `01` to the command characteristic.
5. Add the OLED after its controller and I2C address have been confirmed.
6. Add dock and audio circuits only after the purchased board's GPIO numbering
   and voltage levels are mapped.

## Speaker warning

Do not connect an 8-ohm dynamic speaker directly to an nRF GPIO. The included
firmware expects a `pico-buzzer` PWM devicetree alias, but the physical speaker
needs a suitable transistor/MOSFET driver and power/current design. A piezo
buzzer is a different load and needs its own confirmed wiring. Until an alias
exists, chirps degrade safely to debug logs.

## OLED build on an nRF52840 DK

The development kit already supplies `sw0`, `sw1`, and `led0`. For a common
SSD1306 board connected to its Arduino I2C pins:

```sh
west build -p always -b nrf52840dk/nrf52840 . -- \
  -DDTC_OVERLAY_FILE=overlays/nrf52840dk_ssd1306.overlay
west flash
```

Older nRF Connect SDK releases use board name `nrf52840dk_nrf52840`.

If the module is SH1106, 128x32, address `0x3d`, SPI, or uses different pins,
do not force this overlay. Make a board-specific overlay once the label and
listing are available.

## Custom-board aliases

The adapters discover hardware through devicetree instead of source edits:

| Alias/chosen node | Function | Required? |
| --- | --- | --- |
| `sw0` | Primary interaction button | No |
| `sw1` | Mode-cycle button | No |
| `led0` | Connection/advertising status | No |
| `pico-dock-home` | Active home-dock input | No |
| `pico-dock-car` | Active car-dock input | No |
| `pico-buzzer` | PWM speaker output | No |
| `zephyr,display` | Monochrome OLED | No |

Missing optional nodes produce a log message and leave the rest of Pico alive.
