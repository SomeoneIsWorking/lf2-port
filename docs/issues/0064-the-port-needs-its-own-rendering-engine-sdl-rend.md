---
id: 64
title: The port needs its OWN rendering engine: SDL_Render cannot express what this port draws
status: open
symptom: Three consecutive fixes were each a symptom of the same cause: depth, geometry and lighting are bolted on beside SDL_Render rather than done by it.
tags: reported,renderer,engine,hd2d
created: 2026-08-12
updated: 2026-08-12
---

## What was reported

"make a new engine then", after three commits in a row that each fixed a symptom of the same
root cause.

## The cause, and it is structural rather than a run of bad luck

The port's renderer is SDL_Render with depth, geometry and lighting bolted on beside it. Every
one of the following is a thing that had to be worked around rather than done, and each was
recorded as its own finding before the pattern was named:

1. **SDL_Render has no depth attachment at all** -- `SDL_render.h` contains the string "depth"
   zero times. So authored geometry cannot be drawn through it in any submission order, and the
   port has a SECOND renderer (`mesh.c`) beside the first.

2. **The two cannot share textures.** Claim C032 said the geometry pass could sample the
   textures `render.c` has already uploaded, through the GPU-texture property. FALSIFIED: the
   handle reads back fine and a sample through it from a foreign command buffer returns
   `rgba(0,0,0,0)`, discriminated against three controls in one run. So a stage's art is on the
   GPU **twice**.

3. **One composited quad enters the painter order at one point**, so a set spanning parallax
   depths needs one full-screen colour+depth target per occupied gap -- currently capped at 8,
   with the excess refused. That is a whole render target per gap because the two renderers can
   only meet as a texture.

4. **The light had to be duplicated, and it drifted.** `mesh.c` held a key-light vector fifteen
   degrees from `hd2d.c`'s under a comment saying they were the same numbers, and `hd2d.c`'s is
   not even a constant -- the pause menu sets it. Fixed by extracting `stagelight.h`, but only
   because the two passes are separate programs that have to be kept in step by hand.

5. **`hd2d.c` is a POST-PROCESS over a finished frame**, not shading. It rebuilds a normal from
   the gradient of a sprite's silhouette, because by the time it runs the only thing it has is a
   picture. That is why the five effects that shipped together read as a filter (issue #30) and
   why the bevel is a knob rather than a consequence.

None of these is a bug in SDL. They are all the same fact: **SDL_Render is a 2D sprite API and
this port is drawing a lit 3D scene with pixel-art sprites in it.**

## What the new engine is, and what it is NOT

NOT a rewrite of the port. The recompiler, the runtime, the overrides and the whole Win32/DDraw
shim are untouched. Neither is the display-list RECORDING: `ddraw.c` reaches the renderer through
8 call sites and about ten functions in `render.h`, and that boundary is exactly right -- it is
what turns the game's blit stream into an ordered scene. What changes is what draws it.

One SDL_GPU device, one depth buffer, one texture pool, one light. Sprites and authored geometry
submitted into the SAME pass, ordered by real depth rather than by two painter orders glued
together with render targets.

## The constraint that makes this safe

`tools/e2e.sh render` already diffs the GPU renderer against the software compositor frame by
frame, with a dropped-draw arm proving the comparison can fail. A new engine has to pass the
SAME test against the SAME software compositor. That is the acceptance gate and it exists
already -- this is not a rewrite into the dark.

The old path stays as the A/B control arm, the way `LF2_BG_ORIG` did for the background
override. A reimplementation that cannot be diffed against what it replaces is a rewrite.

## What is explicitly NOT the plan

- Not a big-bang switchover. The engine draws the display list; it becomes the default only when
  it matches the software compositor on the frames the render route already compares.
- Not a chance to redesign the display list. Its shape came out of measurement (issue #42's
  world-band hint, C019's ground markers, the tile pool) and none of that is what is wrong.

### Note (2026-08-12)
### Note (2026-08-12) -- the engine exists and REPRODUCES the renderer it replaces

runtime/video/engine.{c,h} + runtime/shaders/quad.{vert,frag}. One SDL_GPU device (the one the
port's `gpu` renderer is already built on, C029), one D32_FLOAT depth buffer, one texture pool,
three blend pipelines.

MEASURED against the gate that already existed. `tools/e2e.sh render` dumps two frames -- one on
character selection, one in a match -- and diffs them against the software compositor. The
engine:

    frame_000401  max channel diff 1, 251/436700 px differ
    frame_001351  max channel diff 2, 386/436700 px differ

which is the SAME tolerance the old GPU path achieves on the same two frames. Over the run:
1800 frames, 107,459 quads, 225 textures, 0 quads dropped.

THE FIRST VERSION IS DELIBERATELY A REPRODUCTION, and that is not timidity. An engine that both
replaced the renderer AND changed the shading would fail the comparison for two reasons at once
and could not be told apart from a broken one. quad.frag therefore does nothing but sample and
multiply -- no lighting -- and says so in its own header.

THREE PIPELINES rather than one, because SDL_GPU fixes blend state at pipeline creation. The
tempting alternative -- premultiply everything on upload so one blend serves all -- would change
what a keyed sprite's colour IS, and the byte-identity arms of tools/e2e.sh background compare
exact pixels against the software blitter.

DEPTH IS THE LIST ORDINAL, and this is the one design decision in the file. The display list is
painter-ordered and that order is the GAME's answer: it sorts its own sprites on z, and the port
learned the object/shadow pairing from the order the game draws them in (C019). So the engine
does not second-guess it -- position in the list becomes the depth, later is nearer, the test is
LESS_OR_EQUAL so every quad passes. The picture is exactly the painter order's; the DEPTH BUFFER
is new, holds a real value per pixel, and is stored. That is what the old arrangement could not
have at any price and it is what the lighting step needs.

TWO BUGS CAUGHT WHILE WRITING IT, both of the silent kind:

  - the GDI text tiles are premultiplied ARGB WORDS, not RGBA bytes. The first draft memcpy'd
    them, which swaps red and blue -- invisible on white text, and a palette bug a long way from
    here on anything coloured.
  - LF2_ENGINE_DEBUG alone printed nothing, because engine_report rides on render_report's
    900-frame cadence behind LF2_RENDER_DEBUG. The first A/B therefore matched the software
    compositor with NO evidence the engine had drawn anything -- the numbers were identical to
    the old GPU path's, which is exactly what "LF2_ENGINE=1 did nothing" would look like. Both
    switches now, and the run reports 1800 frames drawn.

That second one is why the route has an `engineskip` arm: LF2_ENGINE=1 with LF2_RENDER_SKIP=7
must DIFFER, or the two engine dumps are not the engine.

KNOWN AND NOT HIDDEN: batching is poor -- 96,192 batches for 107,459 quads, about 1.1 quads a
batch. Only CONSECUTIVE quads sharing a texture and a blend mode are merged, and consecutive is
the only grouping allowed because reordering would change the painter order the depth is derived
from. The game interleaves sheets, so consecutive rarely repeats. Worth fixing by giving the
depth real meaning (then order stops mattering and quads can be gathered by texture), which is
the same step that brings the geometry in.

WHAT IS NOT IN IT YET, and both are the reason it exists:
  - stage geometry is still the separate mesh pass. On the engine path an E_MESH entry is
    COUNTED, not drawn -- a silent skip would read as a stage with nothing authored for it.
  - the lighting is still hd2d's post-process over a finished picture.
