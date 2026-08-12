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

### Note (2026-08-12)
### Note (2026-08-12) -- the geometry is IN the engine's pass. Defect 3 is gone.

Hand-woven stage geometry is now a draw call in the middle of the quad stream, into the one
depth buffer, instead of a separate render pass per parallax gap composited back as a texture.

MEASURED, tools/e2e.sh stage_geom, two solids straddling Brokeback Clif's layers:

    1464 geometry draw(s) inside the engine's own pass
    0    separate mesh passes          <- the render target per gap, gone
    1464 draws over 732 frames         <- exactly two a frame, so the two solids kept their
                                          own places in the painter order
    0    geometry draws with no .stage <- the control

Both counters are asserted, because either alone cannot tell "drawn in the sprite pass" from
"composited as a texture as well".

THE ONE HARD PART was reconciling two depth scales. A sprite has no depth of its own -- only a
position in the game's painter order, which IS the game's answer and must not be second-guessed
-- while a solid has a real parallax depth. They meet by giving each recorded piece of geometry
the SLIVER of depth between the two list positions it sits between: ordered against the game's
layers by where the port placed it in the list, and against other geometry in the same sliver by
its own depth. That is exactly what a set needs, because interpenetration is a mesh-against-mesh
problem and two solids in the same gap are the pair that can interpenetrate.

THREE THINGS HAD TO MOVE, and each was a small piece of design rather than plumbing:

  1. THE DISPLAY LIST NOW RECORDS GEOMETRY, NOT A TEXTURE. render_stage_mesh used to take a
     finished SDL_Texture, which meant background.c had to run a whole render pass per gap
     before the list was even drawn. A display list records; it does not draw. It now takes the
     vertices by REFERENCE -- a stage's geometry is loaded once and submitted every frame, so
     that is its natural lifetime -- and background.c builds one persistent buffer per gap when
     the PLAN is built rather than gathering slices every frame.

  2. THE PLAN IS PER STAGE, NOT PER FRAME. It was being recomputed every frame, which was merely
     wasteful before and is plainly wrong now that the list holds references into its output.

  3. THE PLACEMENT BECAME AN AFFINE MAP. mesh.vert used to divide by a view size, which is only
     right when the pass owns the whole target. In the engine the composition is scaled by the
     window and placed inside it, and a wide view shifts the world sideways -- every sprite quad
     already gets that. Geometry drawn into the raw output would have sat correctly in a
     794-wide window and slid out of the stage in any other. So the shader takes a scale and a
     bias per axis; the standalone pass passes the identity, which is why its self-test is
     unchanged.

The SDL_Render path still composites per gap, from render.c, because it cannot take geometry at
all -- kept deliberately so the two paths stay diffable.

NOT REGRESSED: tools/e2e.sh render still green on all ten assertions, engine matching software
at max channel diff 1 and 2. ctest 12/12. gpuguard latch clear.

STILL OUT: the lighting. hd2d is a post-process over a finished picture, reconstructing a normal
from the gradient of a sprite's silhouette because that is all it has by the time it runs. The
engine now has a real depth buffer, stored, with sprites and geometry both in it -- which is the
input that pass never had.

### Note (2026-08-12)
### Note (2026-08-12) -- the codemap's renderer row had become unreadable; split into four

Not a renderer change. A WORKFLOW defect, and it outranks the feature work by the project's own
rule: the codemap is what a session is supposed to CONSULT at the start, and the "Native
renderer" cell had grown to 19,711 characters -- roughly 1,500 words in a single table cell,
accreted one commit at a time. Nobody can read that at the start of a session, so in practice
nobody would, and the map stops doing its job while still looking maintained.

The cause is structural rather than carelessness: every renderer commit appended its findings to
the one cell because there was only one cell. The fix is the split, not a trim.

    Renderer: the engine          engine.c/.h, quad shaders  -- what draws
    Renderer: display list        render.c/.h                -- what records
    Renderer: HD2D lighting       hd2d.c, stagelight.h       -- what shades
    Renderer: stage geometry      mesh.c, stagegeom.c        -- what is authored

19,711 chars -> 1,700..2,900 each. Nothing was invented and nothing load-bearing was dropped:
every claim reference (C010, C018, C019, C021, C029, C031, C032, C033, C034), every hard-won
constraint, and every "this obvious fix is wrong and why" survives. What went is the NARRATIVE --
the sentences that recorded the order things were discovered in, which is exactly what the issue
notes and commit messages already hold and what a status map should not.

Each row now answers what a map is for: what exists, what its status is, what is missing, and the
non-obvious facts a reader would otherwise re-derive.

STILL OVERSIZE and left alone deliberately: the Widescreen row, at 7,292 characters. It has the
same disease and deserves the same treatment, but splitting two subsystems in one pass would make
the diff impossible to check against the originals -- and an unreviewable doc change is how a
load-bearing sentence gets silently lost.

### Note (2026-08-12)
## Verified properly, and two gaps that the fast gate was hiding

Prompted by "have you ever checked the work done so far?". Until this point every change had
been gated on `ctest` plus the ONE render route. Run since:

- **`tools/e2e.sh` full sweep: 19/19, 143 assertions, 0 skipped.** This is what actually clears
  the ground-marker change in `engine_colour_pass` against mouse, background, stage_geom, coop
  and the rest -- none of which had been run against it.
- **Frames looked at, not just counted.** The defocus reads as a defocus on the distant cliff
  (it smooths the art's own dithered stipple and softens the rock edges). Its contribution to
  the flat ground is a max of **1 level over ~40k px**, which is not an effect -- expected,
  since the ground is at the fighters' plane and in focus, but worth stating.

## THE MISSING ARM, and it is the configuration the port is moving to

**No route arm runs `LF2_ENGINE=1` with the lighting ON.** The engine arms all pin
`LF2_HD2D=off`; the light arm leaves `LF2_ENGINE` unset. So the shipped-destination
configuration is untested by the suite.

Measured by hand (engine + hd2d + dof against engine alone, same pad script):

    frame_000401 (character select): max diff 0, 0 px  -- the gate holds
    frame_001351 (match):            max diff 152, 267028 px
      rows 300-349  max 152   the fighters
      rows 350-399  max   1   the ground: the defocus, and it is nothing
      rows 400-449  max 126

So the chain does run on the engine path and its negative still holds there. It is not
asserted anywhere, which is the defect. That arm lands with the commit that makes
`LF2_ENGINE=1` the default -- at which point `englight` BECOMES the light arm rather than
being an eighth 300-second run.

## Fixed here (b611e41), all zero picture change

- `engine_colour_pass` never counted ground markers -- it REPLACES the `PASS_COLOUR` walk that
  `draw_list` counts them in -- so `render_report` printed **"NO ground markers were seen, so
  no shadow was replaced"** while shadows were being drawn. A negative its own method could not
  contradict, on the path this port is moving to. Both counters wired on both paths.
- `stat_ground_orphan` was not gated on the pass, so it counted once per pass. Every orphan
  number reported before this is inflated by the number of passes that ran.
- `engine_colour_pass` did not clear `have_ground` on `E_MESH`. Unreachable today.
- Comment rot: the depth buffer's "the lighting step reads it" is FALSE -- `engine.c` creates it
  `DEPTH_STENCIL_TARGET` only, no `SAMPLER`, and SDL declares no pixel format for depth, so it
  cannot even be wrapped. Three more comments described a half-res blur that was deleted.
- `render`/`background`/`resize` wrote dumps to `/tmp` via `mktemp -d` and **deleted them on
  EXIT**, so a FAILING run destroyed the two frames anyone would want to look at. Now
  gitignored `scratch/`, cleared at the START of the next run. `objects_test.sh` already had
  this fix and its reasoning written down; the other three had missed it.
- The defocus arm selected its frame by the glob `*000401*`, so it matched nothing whenever the
  frame numbers moved and fell through to assert the OPPOSITE thing -- the exact mistake the
  light arm was fixed away from. `light` also joins the missing-frame loop, since those
  comparisons are guarded by a bare `[ -f ]`: an arm whose run died tested nothing and the
  route stayed green.

## The design for the rest of #64 is settled (workflow, 14 agents, 11 returned)

The premise was **confirmed and sharpened** by a code read: `rt_chars` is cleared to 0 and
written only as the literal `1.0` under `SDL_BLENDMODE_NONE` with a discard, so overlapping
fighters form a **binary union** whose interior seam has an exactly-zero mask gradient and
therefore no bevel -- and that is most of a match. `.g` IS per-object (each quad pushes its own
ground row) but never reaches the normal, and two fighters on the same row have identical `.g`
anyway.

**The fix is NOT per-texture normal maps** (my first idea): a quad samples a sub-rect of a
sheet, so a neighbourhood operator leaks across adjacent animation frames, and giving sprites
real normals breaks the G-buffer's exact-`(0,0,0)`-means-billboard marker. It is to **tag the
mask per object**: `hd2d_gbuf.frag`'s `o_gbuf.ba` are hardcoded and read by nothing, `u_geom` is
already pushed and flushed per quad, so the object's own `E_GROUND` ordinal goes in `.ba` and
`mask_gradient` rejects any tap whose tag differs. No new pass, no new target, no new uniform,
no new binding, **no new tunable**.

**The critical catch, which is why this is not landed yet:** a no-op implementation of that fix
**passes the entire suite** -- ctest stays green (the header is correct, the shader is a copy),
the light arm's `hmax>8 && hn>2000` is a lower bound the unchanged bevel already clears, and the
menu arm is zero either way. So the gate comes FIRST: `LF2_HD2D_TAGSTAT=1` reporting *N distinct
tags over M covered pixels, S pixels with a differing neighbour*, asserted on the existing light
arm's stderr in both directions -- `N>=2, S>0` in a match, `N==0, M==0` on the menu.

Caveat on the design's provenance, stated because it affects how much it should be trusted:
3 of 14 agents failed. The "does this matter VISUALLY" lens never returned, so the premise is
confirmed by CODE and not by an independent visual argument; and the "light inside the engine's
fragment stage" design never returned, so that alternative is rejected unopposed rather than
beaten. Full plan: scratch is transient -- the shape is above.
