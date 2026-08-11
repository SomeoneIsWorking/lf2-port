---
id: 41
title: Graphics must scale with the window, per sprite -- not by upscaling the composed screen
status: open
symptom: in a window taller than 550 the game's picture sits in a 550-row band with black above and below; it should fill the window, and it should do so by drawing every sprite larger at draw time rather than by scaling a finished 794x550 frame
tags: reported,rendering,renderer,widescreen,scaling
created: 2026-08-11
updated: 2026-08-11
---

REPORTED 2026-08-11. Filed on receipt.

WHAT IS ASKED FOR, in the reporter's words: "Graphics should scale with the window size but
per-sprite, not the whole screen scaling, like render at a higher res".

So: the picture FILLS the window, and the way it gets there is that each sprite is drawn
bigger, at full precision, into a full-window target -- not that a 794x550 image is composed
and then blown up.

WHERE THE PORT IS TODAY, and why this is a change of position rather than a missing feature.
runtime/ddraw.c's hostwin_window_geometry composes at the window's real pixel WIDTH and the
game's own 550 ROWS, drawn 1:1 and centred, so a 1920x1080 window gets 265 black rows above
and below. That was a deliberate decision and the comment there argues for it at length: the
height cannot follow the window because LF2's vertical screen axis carries z and jump height,
both fixed by stage data, and every layer's picture is 550 rows -- so there is no more WORLD
to show vertically.

THAT ARGUMENT IS ABOUT FIELD OF VIEW AND IT IS STILL TRUE. It is not an argument against
SCALE, and the two were conflated. Showing more world vertically is impossible; drawing the
550 rows of world that exist at twice the size is not. The reporter is asking for the second.

THE OTHER HALF OF THE OLD POSITION -- "an upscale of 1.96 is not an integer, so a game pixel
becomes a block two OR three screen pixels wide" -- is an argument against scaling the
COMPOSED FRAME, which is what the port used to do (550*1920/1080 = 978 wide, scaled up by
SDL). It is not an argument against scaling per sprite, because a per-sprite scale places
each quad at full float precision in a full-resolution target: the geometry is exact and only
the sprite's own texels are magnified. That is the "render at a higher res" in the report.

WHAT ALREADY EXISTS THAT THIS BUILDS ON:
  - The native renderer (runtime/render.c) already has every draw as a QUAD in a display list
    with its own source and destination rect. Scaling per sprite is a transform on that list,
    which is exactly the thing a display list makes cheap. The software compositor cannot do
    it -- it flattens to pixels on the CPU -- so this is native-renderer-only and the soft
    path stays letterboxed. Say so rather than letting the two silently differ.
  - runtime/render.c already magnifies OBJECTS by a whole-number factor (geom_object_scale in
    runtime/overrides/geom.h: a 1080-row window gives fighters 2x) while leaving the stage at
    1x. That is this feature done to half the draws, and the half-done state is itself a
    defect -- a 2x fighter stands on a 1x floor. Whatever lands here should subsume it, not
    sit beside it.

WHAT IS NOT ESTABLISHED and must be measured before the design is fixed:
  - HOW SCALE AND FIELD OF VIEW COMBINE. If scale = win_h/550, a 1920x1080 window shows
    1920/1.96 = 978 px of world width, not the 1920 it shows today. So this change REDUCES
    the widescreen field of view unless the two are decided together. Both are wanted and
    they trade against each other; the reporter has not been asked which they prefer at a
    given window size, and guessing would silently undo issue #20/#39's work.
  - WHETHER A FRACTIONAL SCALE IS ACCEPTABLE ON THE ART. 1080/550 = 1.963. Per-sprite scaling
    fixes the GEOMETRY but a sprite's own texels are still magnified by 1.963, so with
    nearest-neighbour some texel rows are 2 screen px and some are 1. Look at it on a real
    frame before choosing between fractional, integer-floored, or a filter.
  - GDI TEXT is rasterised at the composition's resolution (docs/codemap.md). Scaled up it
    would be a blurry or blocky bitmap while everything else got sharper. It has to be
    rasterised at the FINAL size to honour "render at a higher res", and that is a separate
    change in runtime/gdi.c.
  - The HUD, the pause menu and the controls hint draw straight onto the primary and are in
    NO display list, so a per-sprite scale in render.c does not reach them. They would stay
    at 1x while the world grew. That is the same seam that stops them being lit today.

VERIFICATION IS BLOCKED ON #40 AS THINGS STAND: the arm that would prove this is
tools/e2e.sh render, four GPU runs, and #40 forbids batch headless GPU runs on this machine
until Vulkan validation has been run. A single run is allowed by #40's own plan (step 1 is
"ONE run, not a batch"), so the honest route is one instrumented run at a time.

DO NOT ship the obvious cheap version: setting SDL logical presentation back on and letting
SDL scale the composed frame. That is precisely the "whole screen scaling" the report rules
out, it is what the port already removed, and it would look like progress.

### The main half landed, 2026-08-11

THE PICTURE FILLS THE WINDOW AND EVERY QUAD CARRIES THE SCALE. Two numbers now come off the
window instead of one, and keeping them apart is the whole design:

    scale       = min(win_h/550, win_w/794)     -- how big a game pixel is drawn
    composition = win_w / scale, floored at 794 -- how much WORLD is on screen

The height buys SCALE because there is no more world vertically at any window size; the
leftover width buys FIELD OF VIEW because there is. Both live in runtime/overrides/geom.h
(geom_world_scale, geom_compose_width, geom_compose_rect) and are walked by `ctest geometry`
in a millisecond, and runtime/render.c's draw_list applies the scale PER QUAD as it draws into
a target the size of the window.

WHAT THE ORDER OF OPERATIONS HAS TO BE, since it is the one thing here that is easy to get
subtly wrong: the widescreen centring offset is in the COMPOSITION's pixels and is added
before the scale; the placement of the picture in the window is in the WINDOW's pixels and is
added after. draw_list takes them as two parameters rather than one pre-summed offset for
exactly that reason -- summing them first scales the centring too, and the world slides
sideways as the window grows.

THE SEPARATE OBJECT MAGNIFICATION IS GONE, which is the half-done state this subsumes.
geom_object_scale magnified objects by a whole number about their own base while the stage
stayed at 1:1 -- a 2x fighter on a 1x floor. One scale for the whole list means an object's
size and its position come from the same number.

VERIFIED, on real data:
  - `ctest geometry`, 75 checks including that the drawn height EQUALS the window's height at
    794x550, 1600x550, 1920x1080, 1280x720 and 2560x1440.
  - `tools/e2e.sh widescreen` PASSED with a new second assertion per window, read out of the
    run's own output rather than recomputed: fill / fill / fill / band. The 800x900 case is
    the negative -- it is taller in aspect than the game, so a band is correct there, and a
    build that stretched unconditionally fails that case and no other.
  - `tools/e2e.sh background` PASSED: byte-identical to the recompiled body at 794x550 on both
    dumped frames, with both LF2_BG_SKEW control arms still differing. At the game's own size
    the scale is exactly 1, so the 4:3 game is untouched -- which is what every byte-identity
    arm in the suite rests on.
  - ONE 1920x1080 GPU run (issue #40's own step 1: one run, nothing else on the GPU, journal
    clean before and after), dumping frame 2250 of a match. The frame is 1920x1080 with ZERO
    fully-black rows at the top and ZERO black columns either side; the pixel-width design put
    265 black rows above and below. Stage, fighters, HUD and text are all at the same scale.

WHAT IS NOT DONE, and it is the second half of the original report:

  GDI TEXT IS STILL RASTERISED AT THE COMPOSITION'S RESOLUTION and then scaled up with
  everything else, so it is the one thing in the frame that does not get sharper as the window
  grows -- "render at a higher res" is only half honoured. The fix is in runtime/gdi.c:
  rasterise the tile at the final size. Left separate because it is a different subsystem and
  because the geometry had to be right first.

  THE SOFTWARE COMPOSITOR STRETCHES ONE FINISHED BUFFER, because by the time a frame reaches
  it every sprite has been flattened into those pixels. It is the fallback and it looks like
  one. Not a defect to fix so much as a limit to state -- the whole reason the native renderer
  exists is that a display list can do what a flattened buffer cannot.

  THE PAUSE MENU AND CONTROLS HINT draw straight onto the primary and are in NO display list,
  so they are not scaled per quad either. Same seam that stops them being lit.

  tools/e2e.sh render HAS NOT BEEN RUN against this. It is four GPU runs and #40 forbids
  batches on this machine; the single run above is what stands in, and it checks the framing
  rather than the GPU-vs-software agreement. At 794x550 the scale is exactly 1, so that
  comparison should be unaffected -- should, not verified.

### A SECOND BUG THE SCALE EXPOSED, and it was already there

THE POINTER WAS NEVER MAPPED BACK OUT OF THE WINDOW PROPERLY. `mouse_lparam` in
runtime/win32.c turned a window point into a game point by subtracting `screen_offset_x()`
-- a horizontal offset -- and passing y through unchanged. It leaned on
SDL_RenderCoordinatesFromWindow for the rest, which does NOTHING here: that function undoes
SDL's logical presentation, and this port turns logical presentation off precisely because it
places the composition itself.

That was ALREADY WRONG before this change, vertically: at 1920x1080 the picture was centred
with 265 black rows above it and the pointer's y was never moved to match, so every hit test
in a tall window was 265 rows out. Nobody had noticed because the mouse route runs at the
game's own 794x550, where the offset is zero -- and because the failure is silent. A menu
activates the wrong entry, or none, and a screenshot looks perfect.

A scale would have made it wrong in both axes at once. So the mapping is now the exact inverse
of the placement (geom_window_to_compose, the inverse of geom_compose_rect), applied first, in
window space; `screen_offset_x()` is applied second because it is a COMPOSITION-space offset.

WHY IT IS TESTED OFFLINE AND NOT BY THE MOUSE ROUTE: LF2_CLICK_SCRIPT injects its points
directly in the game's own coordinates (hostwin_inject_pointer), by design, so it bypasses
mouse_lparam entirely and could not catch this in either direction. `ctest geometry` walks the
round trip instead -- every corner and the middle of the composition at five window sizes,
projected out and mapped back -- plus the old mapping's failure as a negative: window row 900
of 1080 is game row 458, and passing it through unchanged gives 900, off a 550-row screen.
