# XIAO Sense + OLED first boot

For Sprint 01 personality, menus, timer pause/resume, optional D1 wiring and
updated migration/validation status, use [the Sprint 01 guide](sprint01.md).

Target: Seeed Studio XIAO nRF52840 Sense, nRF Connect SDK **3.4.2**,
128x64 I2C SSD1306 OLED at address **0x3c**.

This configuration has been checked against the SDK's board definitions and
display binding. A target build and physical-board test are still required.

## Wiring

Unplug USB before moving wires. Connect by the labels on the actual modules:

| OLED | XIAO |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | D4 (P0.04) |
| SCL | D5 (P0.05) |

Leave audio and battery out of this first test. The microphone and IMU are
onboard; they do not need external jumpers. The XIAO reset button resets the
board; it is not the firmware's interaction button. The overlay now configures
an optional focus pushbutton on D0; no MAX98357 audio driver is configured.

## Standalone focus button

With USB unplugged, connect a normally-open momentary switch between **D0
(P0.02)** and **GND**. The overlay enables the internal pull-up; no external
resistor or supply connection is needed. Existing OLED wiring stays in place.

| Switch contact | XIAO |
| --- | --- |
| First switched terminal | D0 |
| Other switched terminal | GND |

A four-leg tactile button usually has two pairs of permanently joined legs.
Use one terminal from each electrical pair. Check that the selected terminals
are open at rest and connected only while pressed; a continuity meter can
identify them. Put the legs on separate breadboard strips so the breadboard
does not short across the switch. Do not rely on leg orientation alone.

- Tap and release while no timer is running: enter Focus mode, smile, and
  start 25 minutes.
- Tap during a running timer: smile without restarting or changing the timer.
- Hold for about one second during a Focus timer: cancel it and return to
  Companion mode. Releasing a hold does not also count as a tap.
- A hold while idle does nothing; tap again after a completed session to
  start another 25 minutes. Break cycling is not automatic yet.
- Cooking and other non-Focus timers are not cancelled by a hold. Car-dock
  driving mode blocks starting a focus session.

Input debounce is 30 ms, sampled from the event loop every 20 ms while idle.
The button has no dependency on a BLE connection. BLE command `01` still
only interacts with Pico and does not start a focus timer. To test completion
quickly, send `02 01` then `04 0A 00 00 00` over BLE for a ten-second Focus
timer. Its visual alert is supported; audio is still unconfigured.

## Build in VS Code on Windows

1. Open the repository in VS Code.
2. In nRF Connect, choose the installed **v3.4.2 SDK and matching toolchain**.
3. Open a terminal for that SDK using the nRF Connect extension. An ordinary
   PowerShell terminal may not have `west` or the compiler on its PATH.
4. Change directory to `pico_firmware` inside your clone.
5. Run this command as one line:

```powershell
west build --no-sysbuild -p always -b xiao_ble/nrf52840/sense -d build-xiao . -- -DDTC_OVERLAY_FILE=overlays/xiao_ble_ssd1306.overlay -DEXTRA_CONF_FILE=configs/xiao_ble.conf
```

The build must report the XIAO Sense board, the XIAO OLED overlay, and both
`prj.conf` and `configs/xiao_ble.conf`. The older DK overlay is for a different
board and is not used here. This is a single-application build using the
factory bootloader and the board's fixed application partition.

Expected output:

```text
pico_firmware/build-xiao/zephyr/zephyr.uf2
```

If compilation fails, capture the first error and its surrounding lines.
Do not flash a partial build or substitute a DK image.

## Upload over USB

1. Connect the board using a USB data cable.
2. Quickly press reset twice; the **XIAO BLE** removable drive appears.
3. Copy **zephyr.uf2** from the successful build to the root of that drive.
4. The drive disappears as the board restarts into Pico.

The removable drive is a bootloader interface, not ordinary persistent file
storage. Do not copy the source folder, `.hex`, or `.elf` onto it. A plain
`west flash` may select an external-debugger runner; the copy step above is
the intended USB-only upload route.

## Expected result

- The factory RGB colour cycle is replaced by Pico's red heartbeat.
- The OLED shows Pico's built-in face renderer.
- A BLE scan should find an advertisement named **Pico**.
- USB serial diagnostics should be available through the board's CDC console.
  Open the board's COM port at 115200 baud; reset once with the terminal open
  if you missed startup messages.

If the LED heartbeat works but the display is blank, check the OLED labels,
ground connection, breadboard row bridges, and header solder joints. Look for
`Display ready: 128x64` or a display initialization error in the serial log.
An OLED at 0x3d needs a matching overlay address; an SH1106 display needs its
own compatible and configuration. Do not assume either from a blank screen.

### Repeating USB diagnostics

The XIAO configuration enables temporary bring-up diagnostics every five
seconds, so reopening the USB terminal after reset does not miss them:

```powershell
python -m serial.tools.list_ports -v
python -m serial.tools.miniterm COM6 115200 --dtr 1
```

Replace COM6 with the board's actual application COM port. Close other serial
terminals first. Exit miniterm with Ctrl+]. Use normal application mode, not
the double-reset bootloader drive.

`[Pico diag]` lines report whether display initialization and frame writes
succeeded, plus address-only probes on the display's I2C bus at 0x3c and
0x3d. A probe result of 0 means an address acknowledged; negative values
are driver errors and do not, by themselves, identify the physical cause.
An ACK does not identify the controller model. The diagnostic redraws the
current frame but does not retry failed device initialization. After fixing
wiring, power-cycle the board to rerun initialization.

Set `CONFIG_PICO_BRINGUP_DIAGNOSTICS=n` in the XIAO configuration when done.

### Pairing troubleshooting build

The XIAO fragment now uses phone-initiated pairing: connect in nRF Connect
for Mobile, then choose Bond once and accept any Android pairing prompt.
Pico does not send an automatic security request immediately on connection.
The encrypted GATT permissions remain in force and Secure Connections pairing
remains required. This change tests a possible initiation/timing conflict; it
does not establish the cause of an earlier timeout.

Temporary SMP debug logging and pairing-complete/failure callbacks provide
more detail if this attempt fails. Capture the log from connection through
failure. `BLE pairing complete; bonded=1` and `BLE link security level 2`
are the expected success indications. Send command `01` only after pairing.

## SDK references

- [XIAO board definitions](https://github.com/nrfconnect/sdk-zephyr/tree/ncs-v3.4.2/boards/seeed/xiao_ble)
- [SSD1306 I2C binding](https://github.com/nrfconnect/sdk-zephyr/blob/ncs-v3.4.2/dts/bindings/display/solomon,ssd1306-i2c.yaml)
- [XIAO UF2 upload instructions](https://docs.zephyrproject.org/latest/boards/seeed/xiao_ble/doc/index.html#uf2-flashing)
