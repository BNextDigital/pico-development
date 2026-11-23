# MAX98357A audio clock and playback

## Finding

The silent speaker has no confirmed root cause. The previous 31,250 Hz
request did produce a nominal 31,250 Hz physical clock. This falls within
the MAX98357A electrical range for its 32/44.1/48 kHz family. Changing the
request alone cannot establish or restore acoustic output.

Reviewed the Pico synthesis and worker, XIAO I²S overlay and audio config,
and the NCS 3.4.2 driver (Zephyr `9133dab6`, nrfx in hal_nordic
`2a63cca4`). `find_suitable_clock` passes a 32 MHz base and 16-bit word
width to `nrfx_i2s_prescalers_calc`. The nRF52840 uses the discrete
divider branch. The first minimum-error candidate wins. Nominal BCLK is
32 times LRCLK for this standard I²S, signed 16-bit stereo controller.
The HFXO feeds the clock, and MCK is internal to the XIAO.

| Requested LRCLK | MCK generator | MCK/LRCLK | Calculated physical LRCLK | Calculated physical BCLK | Error from request |
|---:|---:|---:|---:|---:|---:|
| 31,250 Hz (previous) | 32 MHz / 32 | 32 | 31,250.000 Hz | 1,000,000.000 Hz | 0% |
| 32,000 Hz | 32 MHz / 31 | 32 | 32,258.065 Hz | 1,032,258.065 Hz | +0.806% |
| 48,000 Hz | 32 MHz / 21 | 32 | 47,619.048 Hz | 1,523,809.524 Hz | −0.794% |
| 47,619 Hz (selected) | 32 MHz / 21 | 32 | 47,619.048 Hz | 1,523,809.524 Hz | +0.00010% |

The amplifier lists 32 and 48 kHz nominal and specifies 30.4–50.4 kHz
for their LRCLK range, 32/48/64 BCLK cycles per frame, 16-bit I²S and
a one-bit delay from LRCLK to the MSB. The selected rate is generally
closer to its nominal rate in percent and is above the 40 kHz boundary
where the datasheet specifies a looser RMS clock-jitter limit.
These are arithmetic and interface-spec checks. Measure the pins to
validate oscillator tolerance, duty cycle, jitter and framing on the board.

At 47,619 frames/s, PCM throughput is ~190.5 kB/s from 125.0 kB/s
and a 128-frame block lasts 2.688 ms from 4.096 ms. This is about
52% more samples and DMA handoffs per second of playback. The worker
and four 512-byte slab blocks are unchanged. The app still stops clocks
between cues. No live CPU load, current or DMA IRQ margin was measured.

All cue durations and oscillator phase use the 47,619 Hz integer rate
from the same header as I²S. The reference carries 95,238 PCM frames,
approximately two seconds at the physical rate. Metronome beat
deadlines, notes E2–C6, volume scaling, and peak ceiling are unchanged.
Audio starts about 8.064 ms after clocks begin, with the same lead on
each beat. The reference UI allows 20 ms beyond the two-second PCM
duration for startup and teardown. Playback has no resampling.

Sources: [MAX98357A/B datasheet, electrical characteristics and digital interface](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf),
[nRF52840 product specification, I²S §6.11](https://www.mouser.com/datasheet/2/297/nRF52840_PS_v1.1-1623672.pdf),
[pinned Zephyr I²S driver](https://github.com/nrfconnect/sdk-zephyr/blob/9133dab6b066/drivers/i2s/i2s_nrfx.c).

## Playback findings

- The worker used to wait for a completely free slab before asking the
  driver to DROP. If a previous failure left queued buffers, waiting
  could disable audio before the DROP that would release them. It now
  requests DROP before waiting for ownership to return. It checks
  DROP, START and DRAIN errors and disables I²S only if recovery fails.
- DRAIN used to ignore cancellation. The worker now checks the mute,
  owner and reference generations during DRAIN and drops on a
  cancellation. It still observes the four-block queue policy and the
  existing alarm, timer and finding arbitration.
- Every burst starts with three silent DMA blocks. The MAX98357A
  specifies a turn-on time up to 7.5 ms under its electrical test
  conditions. The lead keeps short cues from starting at the same time
  as the clocks. It does not establish that startup caused sustained
  silence. TX starts with two queued blocks, the final DMA write carries
  whole stereo frames only, and the worker waits for driver-owned
  buffers to return. The worker frees a write-rejected block itself.
- The underlying driver can report IRQ underruns (for example,
  `Next buffers not supplied on time`) separately. A successful
  DRAIN and returned buffers provide MCU-side evidence only.

## Build, tests and resource comparison

Base is main `cf0918aade54` plus the pending Sprint 03 firmware
PR #23, `faca03004806`. Compare against that same base built with the
same NCS 3.4.2 / Zephyr SDK 1.0.1 toolchain.

| XIAO profile | Prior flash | Repair flash | Flash delta | Static RAM |
|---|---:|---:|---:|---:|
| Pico | 332,768 B | 333,652 B | +884 B | 77,836 B, unchanged |
| Optional combined | 397,488 B | 398,224 B | +736 B | 92,256 B, unchanged |

The standard and combined profiles linked under the 600 KiB target and
700 KiB gate. Combined UF2 layout checks passed and activation remains
disabled. The OLED-only profile also linked with audio disabled.
Host regressions cover fixed frame counts, oscillator/note accuracy,
chunk continuity, stereo, clipping/scaling, and the worker's DMA
ownership, cancellation, repeat, saturation and fault recovery. With
`PICO_TEST_NRF_DIR` pointed at the SDK's `nrf` checkout, the host suite
also runs the actual pinned nrfx divider function in a host fixture.
All 42 simulator tests passed, including Music timing, reference,
visual fallback and alert behavior. The DMA model does not simulate
radio coexistence or real interrupt latency.

From the firmware directory in an NCS 3.4.2 terminal:

```powershell
python tools/build_combined.py --sdk-root C:/ncs/v3.4.2 --build-dir build-audio-repair --profile pico
python tools/build_combined.py --sdk-root C:/ncs/v3.4.2 --build-dir build-audio-combined --profile combined
```

Flash the Pico-only `zephyr/zephyr.uf2` first using the XIAO's factory
UF2 bootloader. The audio change does not alter settings, flash
partitions, or bonds.

## Bench acceptance

| XIAO | MAX98357A |
|---|---|
| USB VBUS / 5V | VIN |
| GND | GND |
| D8 / P1.13 | BCLK |
| D9 / P1.14 | LRC / LRCLK |
| D10 / P1.15 | DIN |
| 3V3 | SD / SD_MODE |
| 8 Ω / 1 W speaker leads | OUT+ and OUT− |

1. With power off, verify breakout silkscreen, actual A variant, pin
   order, common ground, jumpers, and that neither speaker terminal
   connects to GND. Do not attach an earth-referenced scope ground to either output. Leave GAIN as wired at the breakout's default.
   With USB on, verify about 5 V at VIN and about 3.3 V at SD_MODE,
   both measured against breakout GND. Recheck VIN while playing.
2. Open the USB serial log. Confirm `I2S configure rc=0`, request
   `47619 Hz`, calculated BCLK `1523809 Hz` (rounded down), then
   `MCU I2S ready`. Use Music → Reference → A4 → Play at default 25%.
   Test the sustained tone before chirps.
3. If available, probe D8/D9/D10 against common ground at the amp
   input. Expect approximately 1.524 MHz BCLK, 47.619 kHz LRCLK, and
   16 bits per channel with the MSB one BCLK after the LRCLK transition.
   DIN is zero for ~8 ms, then carries changing, identical left/right
   samples from the 440 Hz tone for approximately two seconds.
4. Interpret the result using the table below. Once the reference tone
   works, test repeated cues, tempo/accents, changing notes during
   playback, mute and cancellation, a timer/alarm interrupt, and
   operation with the optional amplifier absent.

| What you observe | What to isolate |
|---|---|
| VIN missing or sagging, SD_MODE low, or grounds not common | Supply or enable wiring |
| No BCLK/LRCLK with `I2S job submit … rc=0` | MCU-side runtime/overlay, probe point, jumper continuity. Check START and worker logs. |
| Clocks present, DIN stays zero after lead-in | D10 routing, PCM/volume or probe/decode. Check for the same signal at the XIAO and amp. |
| Clocks and data present with incorrect ratio or alignment | BCLK/LRC swapped, wrong silicon variant/framing, or wrong flashed overlay |
| Clean I²S at amp DIN with steady VIN/SD_MODE, speaker silent | Speaker continuity, OUT+/OUT− wiring and breakout health. Use a known-good speaker/module when available. |
| Quiet clean output | At 25%, PCM is limited to a peak of 512 of 32,768. Verify amp gain/options before changing volume. Keep the existing ceiling. |
| Slab allocation, TX submit, DRAIN or recovery failure | Capture the log from job submission through recovery, plus any driver underrun message. On unreleased DMA blocks, firmware disables I²S until restart and retains visual operation. |

`I2S job submit` reports worker-queue acceptance and `I2S DMA released`
reports returned TX buffers. Successful recurring metronome beats are
quiet in the log. Physical clock captures and acoustic validation are
pending.

## Optional power-on audio diagnostic

From `pico_firmware/` in the nRF Connect SDK 3.4.2 terminal, build the
Combined diagnostic image:

```powershell
python tools/build_combined.py --sdk-root C:/ncs/v3.4.2 --build-dir build-post-combined --profile combined --post-beep
```

The flag adds `configs/xiao_audio_post.conf` to the existing audio
configuration. Without the flag, `CONFIG_PICO_AUDIO_POST_BEEP` defaults
to disabled. The option depends on `PICO_AUDIO_I2S`. The OLED-only
profile needs no extra configuration. Combined still leaves Find Hub
activation disabled.

The boot sequence restores mute and volume, initializes the audio
adapter, brings up the display and Bluetooth, then services Clock
before offering the POST cue to the existing worker. The queue
submission does not wait for playback. A higher-priority audio owner
can suppress a pending beep or preempt one already in progress. A
skipped or failed attempt never automatically retries.

The cue uses the existing bounded 880 Hz triangle oscillator. At the
configured sample rate the tone is 4,761 stereo frames (100 ms) with
238-frame attack and release. The existing three 128-frame startup
silence blocks precede the tone. The saved sound level applies and
there is no PWM fallback for the POST.

### Bench acceptance

1. Keep the previously working Combined UF2. Double-tap the XIAO reset
   button and copy `build-post-combined/zephyr/zephyr.uf2` to the
   bootloader volume.
2. Open the serial console at 115200 baud with DTR enabled. For the
   external UART, keep the console attached across reset. For USB CDC,
   reopen its COM port when it re-enumerates if your terminal drops it.
   A console that attaches late may miss the one-shot logs.
3. Reset or power-cycle once with sound unmuted and a nonzero volume.
   Check the I²S configure result, the POST queue decision, and the
   START/DRAIN results. Listen for one short beep.
4. Verify the OLED, Clock and companion connection operate normally.
   Enable mute, let the preference save, and reset again. Expect a
   muted skip and no POST transfer. A saved zero volume also skips it.
5. Reflash the known-good Combined image, or build Combined without
   `--post-beep`, to check the default silent startup.

A queue acceptance line confirms submission to the audio worker.
`I2S START accepted` confirms the driver accepted the start request.
`MCU-side I2S transmission completed` means DRAIN was accepted and
the driver's DMA buffers were released. START or DRAIN errors and
release timeouts follow the existing DROP recovery path. Acoustic
output requires hearing the beep or measuring the speaker output
separately. No wiring or gain changes are part of this test.
