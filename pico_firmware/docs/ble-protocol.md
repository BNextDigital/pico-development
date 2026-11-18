# Pico BLE protocol v1

Pico advertises one custom 128-bit service:

- Service: `8e7b0001-54f2-4d9b-a3d1-9f150000c001`
- Status: `8e7b0002-54f2-4d9b-a3d1-9f150000c001` (read + notify)
- Command: `8e7b0003-54f2-4d9b-a3d1-9f150000c001` (write)

Status and command access require an encrypted BLE link. The prototype uses
LE Secure Connections/Just Works bonding, so it prevents passive reading and
retains paired identities but does not yet authenticate that a human-approved
phone is the central. A production pairing ceremony can use Pico's screen and
buttons.

## Commands

All integers are little-endian. There is no packed C struct on the wire.

| Bytes | Meaning |
| --- | --- |
| `01` | Interact with Pico |
| `02 mm` | Set mode to `mm` |
| `03 mm` | Set mood to `mm` |
| `04 ss ss ss ss` | Start a timer for 1–604800 seconds |
| `05` | Cancel the running timer |
| `06` | Request a status notification |

Mode IDs are companion `0`, focus `1`, music `2`, cooking `3`, wayfinding `4`,
notes `5`, and driving `6`. Driving cannot be requested remotely; the car dock
owns that transition.

Mood IDs are neutral `0`, happy `1`, curious `2`, sleepy `3`, and alert `4`.

## Status packet

The status characteristic is always exactly 16 bytes.

| Offset | Size | Value |
| ---: | ---: | --- |
| 0 | 1 | Protocol version (`1`) |
| 1 | 1 | Mode ID |
| 2 | 1 | Mood ID |
| 3 | 1 | Dock ID: none `0`, home `1`, car `2` |
| 4 | 1 | Flags: bit 0 connected, bit 1 timer running |
| 5 | 1 | Battery percentage, or `255` when unknown |
| 6 | 4 | Timer seconds remaining |
| 10 | 4 | Lifetime interaction count |
| 14 | 2 | Capability bit field |

Capability bits are display `0`, speaker `1`, microphone `2`, home dock `3`,
car dock `4`, position `5`, and compass `6`.

Unknown commands, wrong lengths, zero-duration timers, invalid enum values,
and attempts to request driving mode are rejected at the GATT boundary.
