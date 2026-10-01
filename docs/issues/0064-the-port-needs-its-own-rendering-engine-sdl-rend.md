---
id: 64
title: The port needs its OWN rendering engine: SDL_Render cannot express what this port draws
status: closed
symptom: Three consecutive fixes were each a symptom of the same cause: depth, geometry and lighting are bolted on beside SDL_Render rather than done by it.
tags: reported,renderer,engine,hd2d
created: 2026-08-12
updated: 2026-09-09
---

## The constraint that makes this safe
The recorded renderer comparison diffs the GPU renderer against the software compositor frame by
frame, with a dropped-draw arm proving the comparison can fail. A new engine has to pass the
SAME test against the SAME software compositor. That is the acceptance gate and it exists
already -- this is not a rewrite into the dark.
The software path stays as the A/B control arm, matching the earlier background comparison
override. A reimplementation that cannot be diffed against what it replaces is a rewrite.

### Note (2026-08-12) -- the engine exists and REPRODUCES the renderer it replaces
runtime/video/engine.{c,h} + runtime/shaders/quad.{vert,frag}. One SDL_GPU device (the one the
port's `gpu` renderer is already built on, C029), one D32_FLOAT depth buffer, one texture pool,
three blend pipelines.
MEASURED against the gate that already existed. The recorded renderer comparison dumped two frames -- one on
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
what a keyed sprite's colour IS, and the recorded background identity arms compare
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

### Note (2026-08-12) -- the geometry is IN the engine's pass. Defect 3 is gone.
Hand-woven stage geometry is now a draw call in the middle of the quad stream, into the one
depth buffer, instead of a separate render pass per parallax gap composited back as a texture.
MEASURED by the recorded stage-geometry scenario, two solids straddling Brokeback Clif's layers:
    1464 geometry draw(s) inside the engine's own pass
    0    separate mesh passes          <- the render target per gap, gone
    1464 draws over 732 frames         <- exactly two a frame, so the two solids kept their
                                          own places in the painter order
    0    geometry draws with no .stage <- the control
Both counters are asserted, because either alone cannot tell "drawn in the sprite pass" from

(full record: git history)
