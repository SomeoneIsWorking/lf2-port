---
id: 52
title: The controls hint and the pause menu switch the whole frame back to the software compositor, so every menu screen is still whole-screen-scaled
status: resolved
symptom: the native renderer is never asked to draw a frame outside a match: the GPU gate in hostwin_present is turned off by hint_on, and menu.c turns the hint on for every screen that is not a match. Measured at 1920x1080: 901 presents, render frames=0. So the menus are a 978x550 buffer point-scaled to the window by SDL -- exactly the whole-screen scaling issue #41 rules out -- and none of the per-quad scale or the outline-font text reaches them
tags: rendering,renderer,widescreen,scaling,menu
created: 2026-08-11
updated: 2026-08-11
---

FOUND while closing out issue #41's remaining seam. #41 records the seam as "the HUD, the
pause menu and the controls hint draw straight onto the primary and are in NO display list,
so a per-sprite scale in render.c does not reach them. They would stay at 1x while the world
grew." That understates it by a lot. They do not stay at 1x -- their PRESENCE turns the
native renderer off for the entire frame, and everything else on that frame is
whole-screen-scaled with them.

THE GATE, runtime/video/ddraw.c hostwin_present:

    const int gpu = hw.renderer && render_gpu_enabled() && !pause_active() && !hint_on
                    && render_present(frame_src_pixels, frame_src_off, w, h);

and runtime/overrides/menu.c:335:

    controls_hint_enable(mode != MODE_IN_GAME);

So the hint is on for the front end, the mode menu, character selection, the loading screen
and the pre-fight overlay -- every screen the player sees before a fight -- and on all of them
`gpu` is 0 before render_present is even called.

MEASURED, one run, 1920x1080, front end only:

    widescreen: window 1920x1080 -> composition 978x550 at scale 1.964,
                drawn into 1920x1080 at (-0,0) -- fills the window
    render: gpu=on frames=0 (software fallbacks=0) quads=0 ... textures=0
    present #901 978x550 renderer=0x16f48a50

901 frames presented, zero of them by the renderer. Note that `software fallbacks=0` too:
both counters live INSIDE render_present, so when the gate skips the call entirely the report
reads as though nothing went wrong. gpu=on frames=0 is the only thing in that line that says
what happened, and it says it by omission.

WHAT THE PLAYER GETS. The 978x550 composition is handed to SDL as one texture and point-
scaled to 1920x1080, so on every menu a game pixel is a ~2x2 block, the outline-font text
from issue #45 is rasterised small and then magnified (the exact "blurry or blocky bitmap"
#41 warned about), and the per-quad scale does nothing. The picture FILLS the window, which
is why tools/e2e.sh widescreen passes and why this was invisible: that test reads the
composition width and the placement rectangle, and both are right. It cannot see WHICH
renderer filled it.

THE CAUSE, and it is one sentence: both pieces of port UI are drawn into the PRIMARY surface
after the game's composition has been copied to it, so they exist only as software pixels,
and the only way to make sure they are not lost is to present the software buffer. The gate
is a correct response to where they draw. Moving the gate is not the fix; moving the DRAW is.

THE FIX has two halves and they are separable, which matters because the second one is much
harder than the first:

  THE CONTROLS HINT is one line of text with no state. It can be appended to the composition's
  display list as a hi-res tile -- render_tile_begin already takes a rasterised size larger
  than its placement, which is exactly what issue #45 added it for -- at the point
  present_primary runs, which is after the copy-to-primary and before hostwin_present, when
  that list is still intact. Then `!hint_on` comes out of the gate. This is most of the
  frames in a run.

  THE PAUSE MENU cannot follow it as it stands, and the reason is worth writing down before
  somebody tries: pause works by NOT CALLING the game's update, so while it is up the game
  records no blits, and render_frame_reset clears the list after every present. The second
  paused frame would have an empty list and render_present would correctly refuse it. Making
  the pause menu a tile therefore also means giving the renderer a way to REDRAW THE LAST
  LIST -- the GPU equivalent of pause.c's `snap` buffer -- and that is a change to the
  renderer's frame lifetime, not a change to where a menu draws.

DO NOT "fix" this by deleting !hint_on and !pause_active() from the gate. The frame would
then be presented by the GPU from the game's list and the hint and the menu, which are only
in the software primary, would simply vanish -- the pause menu would be invisible while
still swallowing input, which is worse than the defect.

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

A LYING INSTRUMENT FELL OUT OF THIS and is recorded as I010. tools/e2e.sh render dumps frames
1300 (character selection) and 2250 (a match). Character selection has the hint up, so its
`gpu` arm was dumping the SOFTWARE buffer: "frame_001300: gpu matches software" was one buffer
compared against itself, and had been for the whole life of the renderer. Its negative control
did not catch it because LF2_RENDER_SKIP is implemented in the display-list RECORDING, so
dropping draws changes what the software compositor composes too -- the control shared a cause
with the positive and went green with the renderer uninvolved. Both frames are real
comparisons now: max channel diff 1 on the menu, 2 on the match.

VERIFIED: ctest 8/8, and tools/e2e.sh smoke mouse resize widescreen background render -- six
of six passing, including background's byte-identity arm at 794x550.

WHAT IS LEFT, unchanged from the entry above: the pause menu. It needs the renderer to be able
to redraw the last list, because pause works by not calling the game's update and the list is
empty from the second paused frame onward.

### Resolution (2026-08-11)
BOTH HALVES FIXED. The port's own UI is in the frame now rather than beside it, and neither
piece turns the renderer off.

THE CONTROLS HINT appends its glyphs to the live composition's display list (game_glyph_tile,
the display-list half of the glyph renderer split out of game_glyph_draw). Menus went from
0 of 901 GPU-drawn frames at 1920x1080 to 900.

THE PAUSE MENU is drawn over a RETAINED frame. Pause freezes the world by declining to call
the game's update, so no blit arrives; a frame's lists are therefore cleared by the first call
that RECORDS over them rather than by the present that finished them, and render_hold_begin
rewinds a held frame's overlay before the next one is recorded. Measured: 573 held frames in a
run, the menu crisp at 1080p over a dimmed frozen picture.

THREE THINGS BROKE ON THE WAY, each with its own cause, and all three are worth keeping:

  THE SOFTWARE PAINTER DESTROYED THE FRAME THE LIST PAINTER WAS ABOUT TO DRAW OVER. pause_draw
  paints the primary through the same glyph renderer the game's text uses, and that renderer
  records a tile as a side effect -- a recording call, which is what clears a spent frame. It
  did not fail loudly: the entries came back when the list was rewound, but the tile arena
  underneath them had been overwritten, so the frozen frame's TEXT came out as garbage while
  everything drawn from a cached texture looked perfect. The list is recorded first now.

  THE TILE POOL FILLED AND SILENTLY DROPPED 42402 TILES. It was keyed on exact size, which
  rests on "a tile's size repeats constantly" -- true of a glyph, one 8x16 cell scaled, and
  false of h_TextOutA, whose tile is a whole string and so is as wide as that string. And the
  bound is not distinct sizes at all: every tile in a frame is live at once, so the pool must
  be at least as large as the busiest frame. Sizes are bucketed to 32 and a texture serves any
  tile that fits in its corner; the cap is 512 against a measured peak of 121, which the
  report prints beside it.

  THE LIGHTING RE-COMPOSITED A FIGHTER ON TOP OF THE PAUSE PANEL. The character mask is built
  from the game's own objects, and drawing the overlay before hd2d_post put those pixels back
  over the menu at full brightness. The list now carries an overlay boundary: everything
  before it is the game's picture and is lit, everything from it on is drawn onto the finished
  target afterwards.

VERIFIED. `ctest` 9/9 in 1.97 s -- including the new `framelife` test, which walks the
retained-frame bookkeeping and the pool offline (issue #53). All 13 e2e routes pass, and the
two that can see this change specifically: `render` compares GPU against software on both its
frames (max channel diff 1 on the menu, 2 on the match) and `pause_dropout` asserts the
renderer drew the paused frames. `background`'s byte-identity arm at 794x550 still holds.

AND ONE LYING INSTRUMENT, recorded as I010: render_test dumps frame 1300, character selection,
which has the hint up -- so its `gpu` arm was dumping the SOFTWARE buffer and "gpu matches
software" was one buffer compared against itself for the whole life of the renderer. Its
negative control could not catch that, because LF2_RENDER_SKIP is applied in the display-list
recording and so changes what the software compositor composes too: the control shared a cause
with the positive and went green with the renderer uninvolved.
