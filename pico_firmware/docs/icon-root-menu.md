# Icon root menu

The root menu shows one item at a time: a 32x32 pixel icon, a centered
10-pixel-high caption, navigation chevrons and nine position dots on the
128x64 OLED. Navigate cycles items; Action opens the selected item. Existing
Back/hold controls and selection restoration remain unchanged.

Order: Focus, Cooking, Home, Music, Wayfinding, Notes, Themes, Settings, Back.
Wayfinding and Notes still show the existing unavailable-feature screen.
Utility and Settings screens retain their current layouts.

Nine original 16x16 one-bit icons occupy 288 bytes of packed pixel data in
`menu_icons.hpp`, plus labels/metadata. The renderer scales them by two without
allocating another framebuffer. Surfaces below 96x56 use a compact 16x16 icon
and five-pixel caption. No periodic redraws or animation work were added.
The desktop simulator uses this same production renderer.

## Validation

Run `bash tests/host/run.sh` and `bash simulator/run-tests.sh` from
`pico_firmware`. Regression checks cover all nine distinct menu frames,
caption/icon visibility, framebuffer layout equivalence, compact odd-sized
surfaces, carousel wrapping and selecting/returning from Music.

The Nordic SDK is unavailable in the implementation environment. Target
compilation and physical OLED readability remain to be checked. No new wiring
or build profile is required: rebuild your existing XIAO profile and copy its
`zephyr/zephyr.uf2` to the bootloader drive. On the device, check every icon and
caption, wrapping, selection, Back and that utility screens still work.
BLE IDs and persisted settings are unchanged.
