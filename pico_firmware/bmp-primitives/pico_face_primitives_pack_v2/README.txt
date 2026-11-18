Pico Face Primitives Pack v2
=============================

Purpose
-------
A modular 1-bit BMP primitive library for composing Pico's face in firmware.

Target
------
128x68 monochrome OLED

Pack structure
--------------
1) base_families/
   Eight starter base families:
   - modern
   - chibi
   - anime
   - retro_ibm
   - mecha
   - vintage_cartoon
   - minimal_emoticon
   - kawaii_mascot

   Each style includes composable primitives such as:
   - eyes
   - brows
   - mouths
   - noses
   - cheeks

2) expression_modifiers/
   Reusable emotion and theme overlays:
   - emotion_symbols
   - seasonal_spooky
   - robotic_hud
   - creature_addons

3) overlays_and_themes/
   Larger overlays for whole-face compositing:
   - hud_frames
   - manga_fx
   - minimal_ui

4) samples_128x68/
   Demo composites showing how layers can be combined.

Rendering notes
---------------
- All .bmp assets are 1-bit monochrome.
- White = lit pixel, black = off pixel.
- Most component assets are tightly cropped to reduce memory footprint.
- Use the manifest files for dimensions and categories.
- Compose by blitting multiple masks onto a 128x68 face buffer.

Suggested firmware model
------------------------
Base family (eyes + brows + mouth + nose)
+ optional cheeks
+ optional expression symbol
+ optional theme overlay
= current face

That should let Pico swap personality, mood, and shell-matching face styles without
requiring full-screen art for every expression.
