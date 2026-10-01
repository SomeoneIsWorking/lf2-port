---
id: 52
title: The controls hint and the pause menu switch the whole frame back to the software compositor, so every menu screen is still whole-screen-scaled
status: closed
symptom: the native renderer is never asked to draw a frame outside a match: the GPU gate in hostwin_present is turned off by hint_on, and menu.c turns the hint on for every screen that is not a match. Measured at 1920x1080: 901 presents, render frames=0. So the menus are a 978x550 buffer point-scaled to the window by SDL -- exactly the whole-screen scaling issue #41 rules out -- and none of the per-quad scale or the outline-font text reaches them
tags: rendering,renderer,widescreen,scaling,menu
created: 2026-08-11
updated: 2026-09-09
---

### Note (2026-08-11)
THE HINT HALF IS FIXED, 2026-08-11. The pause half is not, and this stays open for it.
WHAT CHANGED. gdi.c's glyph drawing was one function doing two things -- a CPU blend into a
surface, and a premultiplied display-list TILE for the renderer. The tile half is now
`game_glyph_tile(ch, x, y, ink, dst_pixels)` on its own, so a caller that is NOT writing the
software frame can still put text in a list. controls_hint_draw calls it with the COMPOSITION's
pixels (the surface the renderer builds its frame from) and keeps drawing pixels on the PRIMARY
for the software path. Then `!hint_on` came out of the gate in hostwin_present.
The timing is what makes this work and is worth stating: present_primary runs AFTER the
copy-to-primary and BEFORE hostwin_present, so the composition's list is still intact and the
tiles land at its END -- which is exactly where a hint drawn over the frame belongs. The
centring is already baked into the composition (frame_source_note passes off 0), so one x
serves both destinations.
MEASURED, same run both times, 1920x1080, front end, 901 presents:
    before   render: gpu=on frames=0   (software fallbacks=0) quads=0 tiles=0 textures=0
    after    render: gpu=on frames=900 (software fallbacks=0) quads=3599 fills=1801
                     tiles=50400 textures=3, 56 tile allocations = 0.062/frame
The menus are now drawn per quad into a 1920x1080 target instead of being a 978x550 buffer
point-scaled by SDL, so the outline-font text from issue #45 finally reaches them. Confirmed
on a dumped frame 900: the GPU arm dumps 1920x1080, the LF2_RENDERER=soft arm of the same
frame dumps 978x550, which is the difference stated as a number.
Tile allocations stay flat at 0.062/frame, which is the counter issue #40 asks to watch, and
`journalctl -k -b 0` reports ZERO ring timeouts, GPU resets, illegal opcodes or lost VRAM
across every run in this session.
A LYING INSTRUMENT FELL OUT OF THIS. The retired renderer comparison dumped frames
1300 (character selection) and 2250 (a match). Character selection has the hint up, so its
`gpu` arm was dumping the SOFTWARE buffer: "frame_001300: gpu matches software" was one buffer
compared against itself, and had been for the whole life of the renderer. Its negative control
did not catch it because LF2_RENDER_SKIP is implemented in the display-list RECORDING, so
dropping draws changes what the software compositor composes too -- the control shared a cause
with the positive and went green with the renderer uninvolved. Both frames are real
comparisons now: max channel diff 1 on the menu, 2 on the match.
VERIFIED: ctest 8/8, and tools/e2e.py smoke mouse resize widescreen background render -- six
of six passing, including background's byte-identity arm at 794x550.
WHAT IS LEFT, unchanged from the entry above: the pause menu. It needs the renderer to be able
to redraw the last list, because pause works by not calling the game's update and the list is
empty from the second paused frame onward.
