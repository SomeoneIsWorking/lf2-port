---
id: 62
title: Stage mode: hand-weave the levels in 3D so HD2D has real depth to light
status: closed
symptom: Stage mode's levels are flat 2D layers, so the HD2D pass has nothing to light: the depth it needs is not in bg.dat and cannot be derived from it. The ask is per-stage 3D geometry, authored by hand, that the levels are woven into.
tags: reported,renderer,hd2d,stage
created: 2026-08-12
updated: 2026-09-09
---

## Constraints this must respect
- **It is a stage-mode-first feature but not a stage-mode-only one**: VS stages use the same
  bg.dat records, so whatever describes the geometry has to sit beside a background record
  rather than inside stage mode's logic.
- **The data lives in the repo, the ART does not.** Geometry authored here is the port's own
  work and is committable; anything derived from the shipped binary or the game's own files is
  not.
- **The software compositor must keep working.** It has no depth buffer and never will, so the
  geometry has to be additive -- a stage with no authored geometry draws exactly as it does
  today, and the recorded background comparison stays byte-exact.

### Note (2026-08-12) -- the first concrete work order, from issue #23
Asked how the port should fill the columns a stage's backmost background layer leaves empty on
a widescreen window -- stretch, tile, mirror, edge-clamp or letterbox -- the answer was "hand
weave". So issue #23 folds into this one, and it arrives with its measurement already done.
THE BACKDROP HALF OF THE AUTHORING, per stage, measured offline from every shipped bg.dat plus
each layer's BMP header (issue #23's note has the full table):
    Forbidden_Tower   sky.bmp     797 px, span  797   static: no more picture at all
    The_Great_Wall    sky.bmp     800 px, span  800     "
    Lion_Forest       forests.bmp 800 px, span  800     "
    Queen's_Island    qi1.bmp     800 px, span  800     "
    Tai_Hom_Village   5.bmp       800 px, span  800     "
    HK_Coliseum       back1.bmp   794 px, span  794     "  (and the stage itself is only 794)
    Template1/2/3     pic1+pic2   967 px total
    Brokeback_Clif    bc1+bc2+bc3 1379 px total
    CUHK              floor1 x2   1594 px total
    Stanley_Prison    wall.bmp    LOOPS at 277 -- nothing to author, it already fills
Against a 978-wide view (a 1920x1080 window) the six static ones leave 178-184 columns empty,
about 18% of the frame. Against an ultrawide 2542 view they leave 1742.
WHY THIS IS THE RIGHT FIRST PIECE rather than the 3D geometry: it is the smallest thing that
needs the same substrate -- a per-stage, in-repo, hand-authored file loaded beside bg.dat and
drawn by runtime/overrides/background.c -- and it has an unambiguous acceptance test, because a
stage with no authored file must draw byte-identically to today (the recorded background comparison). The
depth buffer of issue #49 is not needed to place a 2D extension, so this half can land first
and prove the format while the renderer work proceeds separately.
WHAT IS STILL UNDECIDED: the format, and whether the art is authored per stage by hand from the
start or a first stage is done end to end to find out what the format needs.

### Note (2026-08-12) -- the renderer half has a shape, and it is additive
Issue #49's dependency was recorded as "the first commit is a depth buffer, and that is a
renderer change of the same size as issue #30's original work". Reading SDL 3.4's headers
narrows it a great deal, and #49 now carries the detail. In short:
  - `SDL_render.h` has no depth attachment at all -- that part was right.
  - But it exposes `SDL_GetGPURendererDevice()`, and the port already asks for the `gpu`
    backend by name. So a hand-rolled SDL_GPU pass can share the SAME device: it renders the
    woven geometry into its own colour target with its own depth attachment, and hands the
    finished texture to the existing display list as an ordinary quad.
Nothing in `render.c` is rewritten, the software compositor is untouched, and a stage with no
authored geometry submits no pass -- which is what keeps the recorded background comparison byte-exact.
WHY A SET DOES NOT NEED A SHARED DEPTH BUFFER WITH THE SPRITES: the levels are BEHIND every
fighter. Mesh-against-mesh interpenetration is the case that genuinely needs depth, and it is
entirely inside the offscreen pass; LF2's own painter order keeps placing the sprites. That
distinction is what makes the first commit small rather than the whole renderer.
SO THE TWO HALVES OF THIS ENTRY CAN PROCEED INDEPENDENTLY:
  1. the per-stage authored-data substrate -- a file in the repo, loaded beside bg.dat, drawn
     by background.c. Its first consumer is issue #23's flat backdrop extension, which needs no
     depth at all.
  2. the offscreen SDL_GPU mesh pass, which needs no authored data to be built and tested
     (a cube in the middle of a stage is a sufficient first target).
Neither blocks the other, and (1) is what tells (2) what the format has to carry.

(full record: git history)
