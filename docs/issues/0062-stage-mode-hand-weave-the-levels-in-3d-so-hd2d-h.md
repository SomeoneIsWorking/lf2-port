---
id: 62
title: Stage mode: hand-weave the levels in 3D so HD2D has real depth to light
status: resolved
symptom: Stage mode's levels are flat 2D layers, so the HD2D pass has nothing to light: the depth it needs is not in bg.dat and cannot be derived from it. The ask is per-stage 3D geometry, authored by hand, that the levels are woven into.
tags: reported,renderer,hd2d,stage
created: 2026-08-12
updated: 2026-08-13
---

## What was reported

> also stage mode 3D hand-weave levels for better HD2D

A feature request, and the largest one in the queue. HD2D means sprites in a lit 3D set; this
port has the sprites and (issue #49) not yet the set.

## Why the current renderer cannot get there on its own

`runtime/video/hd2d.c` lights a composition that is, geometrically, a stack of flat layers.
`bg.dat` gives each layer a picture, an x, a y, a scroll `width:` and a `loop:` -- a parallax
recipe, not a scene. `zboundary:` gives the walkable band's two rows and nothing else. There is
no wall, no floor plane, no height for anything, and none of it is recoverable from the data
file: the depth simply was never authored, because LF2 never needed it.

So this cannot be an RE task. There is no original mechanism to port -- which makes it the one
kind of work in this repo where inventing is correct, and it must be labelled as such rather
than dressed up as a finding.

## What "hand-weave" has to mean concretely

Per stage, authored geometry that the existing 2D layers are mapped onto:

- a **floor plane** through the `zboundary:` band, which is the one piece the data DOES pin --
  LF2's depth axis projects straight down the screen, so the band's two rows are the floor's
  near and far edges and the mapping from a fighter's z to a world position is already known.
- **billboard depth per layer**: each bg.dat layer placed at a z, so parallax comes out of the
  camera rather than out of `width:`. The existing `width:` is the constraint that has to be
  reproduced, not discarded -- a layer placed at the wrong z scrolls wrong, and that is
  measurable against the current renderer.
- **hand-authored solids** where a stage has them: the pillars in the throne room, the walls of
  the cave. This is the part that is genuinely new art data.

## Constraints this must respect

- **It is a stage-mode-first feature but not a stage-mode-only one**: VS stages use the same
  bg.dat records, so whatever describes the geometry has to sit beside a background record
  rather than inside stage mode's logic.
- **The data lives in the repo, the ART does not.** Geometry authored here is the port's own
  work and is committable; anything derived from the shipped binary or the game's own files is
  not.
- **The software compositor must keep working.** It has no depth buffer and never will, so the
  geometry has to be additive -- a stage with no authored geometry draws exactly as it does
  today, and `tools/e2e.sh background` stays byte-exact.

## Dependency

Issue #49 is the prerequisite and says so: its first commit is a depth buffer, and there is
nothing to weave levels into until the renderer can express depth at all. This entry is the
thing #49 is for.

## Open, and needs a decision before any code

Where the geometry is authored and in what format -- a per-stage file beside `bg.dat`, a single
table, or something generated from a tool -- is not decided. Nor is whether the first stage is
done by hand end to end (to find out what the format needs) or the format designed first.

### Note (2026-08-12)
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
stage with no authored file must draw byte-identically to today (tools/e2e.sh background). The
depth buffer of issue #49 is not needed to place a 2D extension, so this half can land first
and prove the format while the renderer work proceeds separately.

WHAT IS STILL UNDECIDED: the format, and whether the art is authored per stage by hand from the
start or a first stage is done end to end to find out what the format needs.

### Note (2026-08-12)
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
authored geometry submits no pass -- which is what keeps `tools/e2e.sh background` byte-exact.

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

### Note (2026-08-12)
### Note (2026-08-12) -- the work order is a TOOL now, and it corrects the note above

tools/re/stage_gaps.py replaces the hand-run measurement in the note above and in issue #23. It
reads every bg.dat with tools/re/decrypt_dat.py, takes each layer's real picture width from its
BMP header (bg.dat never states it -- `width:` is the SPAN, i.e. the scroll range), walks the
backmost RUN of layers laid end to end, and prints the columns left over at a given view.

At a 978-wide view (a 1920x1080 window):

    Brokeback_Clif   backmost bc1+bc2+bc3 reaches 1379 -- covers the view
    CUHK             backmost floor1+floor1 reaches 1594 -- covers the view
    Stanley_Prison   backmost layer LOOPS -- nothing to author
    Forbidden_Tower  sky.bmp        reaches  797 ->  181 columns (18%)
    The_Great_Wall   sky.bmp        reaches  800 ->  178 columns (18%)
    HK_Coliseum      back1.bmp      reaches  794 ->  184 columns (18%)
    Lion_Forest      forests.bmp    reaches  800 ->  178 columns (18%)
    Queen's_Island   qi1.bmp        reaches  800 ->  178 columns (18%)
    Tai_Hom_Village  5.bmp          reaches  800 ->  178 columns (18%)
    Template1/2/3    pic1+pic2      reaches  967 ->   11 columns (1%)

9 of 12 short. At 2542 (an ultrawide window) it is 11 of 12, the worst being 1745 columns.

The multi-piece backdrops are the reason this is a tool rather than a table: Brokeback Clif's
three cliff sections, CUHK's doubled floor and the Templates' pic1+pic2 pair all COVER a 978
view and would have been listed as needing work by any per-layer rule. The earlier note's
"6 of 12" counted only the single-picture stages and missed the Templates' 11-column shortfall
entirely.

Negatives it can print, which is what makes the positives mean something: an empty or missing
game tree exits 2 saying it searched NOTHING; a stage whose backmost layer loops is named as
needing nothing rather than omitted; and `--all` prints the WRONG predicate (every non-looping
layer narrower than the view) beside the explanation of why it is wrong, so the prop trap
cannot be rediscovered as a finding.

### Note (2026-08-12)
### Note (2026-08-12) -- the renderer half is de-risked end to end, measured not reasoned

Both remaining "does SDL even do this" questions are now answered by spikes run against BOTH
classes under gpuguard (claims C029 and C030), so the offscreen mesh pass has no unknowns of
that kind left:

    SDL_GetGPURendererDevice(renderer)          -> a real device on the port's own `gpu`
                                                   renderer; driver=vulkan. The `software`
                                                   renderer returns none, which is the negative.
    D32_FLOAT as a depth-stencil target          -> SUPPORTED, and a 256x256 depth texture
                                                   allocates.
    SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER  -> wraps an SDL_GPUTexture as an ordinary
                                                   SDL_Texture, and reading the property back
                                                   off the wrapper returns the IDENTICAL
                                                   pointer. SDL_RenderTexture draws it.

SO THE PIPELINE IS, with no copy anywhere in it:

  1. take the device off the renderer the port already creates
  2. allocate a colour target (COLOR_TARGET|SAMPLER) and a D32_FLOAT depth target
  3. draw the stage's woven geometry into them with a depth-tested graphics pipeline
  4. wrap the colour target as an SDL_Texture
  5. hand it to the existing display list as the BACKMOST quad

render.c is not rewritten, the software compositor is untouched, and a stage with no authored
geometry allocates nothing and submits nothing -- which is what keeps tools/e2e.sh background
byte-exact.

WHAT IS STILL UNMEASURED, and the claims say so rather than implying otherwise: the spike drew
the wrapped texture but did not read its pixels back, so "draw=OK" is the API accepting it and
not yet a picture; and no graphics PIPELINE has been built against the depth format, only a
texture allocated. Both are first-commit work rather than feasibility risks.

### Note (2026-08-12)
### Note (2026-08-12) -- FIRST COMMIT LANDED: the depth-tested pass exists and is proven

runtime/video/mesh.{c,h} plus runtime/shaders/mesh.{vert,frag}. What it is:

  - It takes the device off the renderer the port already creates (claim C029), so there is
    ONE device and render.c is unchanged apart from one init call and one report call.
  - A graphics pipeline with a D32_FLOAT depth attachment, depth test LESS, depth write on,
    clearing depth to 1.0. Culling is deliberately OFF for now -- hand-authored geometry with
    an inconsistent winding would vanish in patches, which reads as a hole in the model rather
    than the authoring mistake it is; it goes on when the format has a validator.
  - It renders into its own colour target, cleared to TRANSPARENT rather than to a colour,
    because the display list composites it over the game's own layers.
  - The colour target is wrapped as an ordinary SDL_Texture (claim C030) -- the same object, no
    copy, no readback -- for the display list to place.
  - A vertex buffer that grows and never shrinks, because a stage's geometry is authored once
    and submitted every frame.

WHAT IS PROVEN, and how. The pass ships a self-test that submits the one case a broken depth
test cannot survive: a NEAR triangle first and a FAR one drawn over it. Two pixels are read
back, not one:

    mesh selftest: overlap pixel rgb(255,0,0) -- the NEAR triangle SURVIVED, so the depth test
                   is running, though it was submitted FIRST and the far one over it
    mesh selftest: far-only pixel rgb(0,0,255) -- the far triangle DID draw, which is what
                   rules out a pass that simply drew nothing
    mesh selftest: PASS

The second line is load-bearing: without it an empty target would report the near triangle as
surviving, because its absence is indistinguishable from its survival at a pixel the far one
also failed to cover.

RUN AGAINST BOTH CLASSES. With `enable_depth_test` set to false the overlap pixel comes back
rgb(0,0,255) and the self-test prints FAIL. The route says how to break it on purpose, so the
next person to touch the pipeline can check the test still discriminates.

The self-test runs at INIT rather than from render_report, which is behind LF2_RENDER_DEBUG and
fires every 900 frames -- a self-test needing two switches and a long run is the same as not
having one. tools/e2e.sh mesh is what runs it.

tools/build/build_shaders.sh now compiles *.vert as well as *.frag, taking the stage from the
extension; ctest shaders covers all five blobs.

WHAT IS NOT DONE, so the pass is not overread: nothing submits any geometry yet. mesh_report
says so out loud rather than printing a clean zero. The next pieces are the stage-space camera
(from the parallax rates and zboundary, both already read by the port) and the authored-data
format, and neither is blocked by the renderer any more.

### Note (2026-08-12)
### Note (2026-08-12) -- HALF THE AUTHORING IS NOT NEEDED: the depths are in the shipped data

The entry's plan said each bg.dat layer would have to be "placed at a z" by hand, with the
existing `width:` as the constraint to reproduce. That has it backwards. The z is ALREADY THE
`width:`, and can be read straight out of it (claim C031).

fn_0041a250 offsets a layer by -((span - 794) * camera) / (stage_width - 794), so its scroll
RATE is (span - 794)/(stage_width - 794). A camera panning past a point at depth z shifts it as
1/z, so that ratio IS a perspective divide and

    depth / depth_of_the_fighters' plane  =  1 / rate

`geom_layer_depth` in runtime/overrides/geom.h, `tools/re/stage_gaps.py --depth` to print it.

WHY IT IS AN IDENTIFICATION AND NOT A PLAUSIBLE FORMULA OVER TWO NUMBERS. On all 12 shipped
stages the depths come out in each stage's own DRAWING ORDER, which nothing in the arithmetic
forces -- Tai Hom Village is 134, 17.5, 13.9, 1.75, 1.45, 1.33, 1.11, 1.00 in file order; CUHK
puts its sky at 4.66, its buildings at 2.1-2.6 and its front floor at 1.00. And it predicts
something no ordering argument could have suggested: The Great Wall's road3 has span 2600 on a
2400 stage, so rate 1.125 and depth 0.89 -- IN FRONT of the fighters. That is exactly what the
layer is, the strip at y 481 along the bottom of the screen.

WHAT THIS CHANGES FOR THE FEATURE:
  - Every existing layer can be placed in the 3D set from the game's own numbers. Nobody
    authors a depth for the sky, the hills, the floors or the props.
  - The parallax is then a CONSEQUENCE of the camera rather than something to reproduce, which
    is also the acceptance test: a quad at a layer's derived depth must scroll exactly where
    the 2D layer scrolls, at every camera position and every view width.
  - What is left to author is genuinely new: the solids a flat layer only implies (the walls of
    a cave, the pillars of a hall), and the columns issue #23 measured.
  - Two stages have NO derivable depth and say so rather than defaulting: HK Coliseum, whose
    stage is 794 wide so nothing pans, and any layer whose span is 794 or less, which never
    moves and is infinitely far. geom_layer_depth returns 0 for UNKNOWN in both, and its header
    says why a caller must not read that as "at the fighters' plane" -- doing so would put
    every stage's sky into the fight.

The 13-check mutant (a constant depth) fails ctest geometry, so the test discriminates.

### Note (2026-08-12)
### Note (2026-08-12) -- THE NEXT STEP IS A FORK, and it needs deciding before the camera exists

With the depths in hand (C031) the obvious next piece is the camera that turns stage (x, y, z)
into clip space. Working it out against the game's own projection turns up a fork that must not
be guessed at, so it is recorded here rather than resolved by whoever writes the matrix first.

WHAT THE GAME'S PROJECTION ACTUALLY IS. A 2D layer at depth z draws at `lx - camera/z`: its
authored x, shifted by the camera divided by its depth. Note what is NOT there -- a SCALE. LF2
draws every layer's picture at its authored size no matter how far away it is. So the game's
projection shifts by 1/z and magnifies by 1. That is not a perspective projection; it is an
orthographic one with a per-depth translation, which is what "2.5D" means here.

THE FORK:

  A  MATCH THE GAME. Orthographic, translation by 1/z, no foreshortening. Every existing bg.dat
     layer then sits in the 3D set at its derived depth and lands EXACTLY where it lands today,
     which is both the faithful answer and an exact acceptance test: a quad at a layer's depth
     must scroll where the 2D layer scrolls, at every camera position and every view width.
     Hand-woven solids get parallax and lighting but no vanishing point. Worth noting this is
     also what the HD2D look usually is -- an orthographic camera with a tilt, not a wide lens.

  B  TRUE PERSPECTIVE. Solids foreshorten properly, which is what most people picture when they
     hear 3D. But then the existing layers CANNOT be placed in it: a sky at depth 4.66 would be
     drawn at a fifth of its size, so every stage's own art would have to be re-authored to sit
     in the new camera. That is a much larger feature than this entry describes, and it changes
     the game's picture rather than adding to it.

WHAT IS NOT ESTABLISHED, and blocks the vertical half of EITHER answer. There are two depth
notions in the data and it is not known whether they are the same axis at the same scale:
  - a LAYER's depth, from its parallax rate (C031), which is what this note is about;
  - a FIGHTER's z, which runs between bg.dat's `zboundary:` rows and is where the walkable
    floor is ON SCREEN (C021, validated on 12 of 12 stages).
Physically they must be the same thing -- something further into the stage is further away --
but whether a fighter at the far zboundary row is at the same depth as a layer of rate r, for
which r, has not been measured. The camera's TILT is exactly that relationship: it is what
decides how much of a floor is visible, and getting it from a guess would be the "magic
placement constant" this project refuses.

MEASURING IT is a bounded piece of RE rather than an open question: the game projects a
fighter's z to a screen row somewhere in fn_0041a5a0's draw (already hand-ported in
runtime/overrides/objects.c) and to a shadow position, and reading that mapping gives the
scale directly. That is the next thing to do, and it should be done before any matrix is
written.

NOTHING HAS BEEN BUILT for the camera. The mesh pass takes a view matrix from its caller
precisely so this decision has somewhere to land.

### Note (2026-08-12)
### Note (2026-08-12) -- THE FORK RESOLVES, and the reason is that the game is not one camera

The note above said the vertical half was blocked on whether a fighter's z and a layer's
parallax depth are the same axis. They are not, and the port already had both halves recorded
-- no new RE was needed, only reading C018 and C021 next to C031.

WHAT A FIGHTER'S z DOES. C018: an object holds x at +0x10, jump height at +0x14 and z at +0x18,
and fn_0041a5a0 depth-SORTS on +0x18. runtime/overrides/objects.c then draws that object's
shadow at `y = [o+0x18] - shadow_height/2` and its tags at `[o+0x18] + 3`. So +0x18 is used
DIRECTLY as a screen row: LF2's depth axis projects down the screen at slope exactly 1, one row
per unit of depth. C021's zboundary rows (Brokeback Clif's 300 and 510) are that same field's
bounds, and C018 confirms it independently -- pressing up walked +0x18 to exactly 300.

So the vertical projection needs no guess and no magic constant: screen_y = z - jump_height,
slope 1. That IS the camera's tilt, measured.

WHAT A FIGHTER'S z DOES NOT DO. It does not affect the horizontal parallax. Every object shifts
by `- cam`, flat, whether it stands at z 300 or z 510 -- objects.c's draws show it. A layer, by
contrast, shifts by `- cam/depth`. So across the 210 rows of the walkable band the game uses NO
parallax variation at all, while between layers it uses 1/z.

THAT IS TWO DIFFERENT CAMERAS GLUED TOGETHER, and it settles the fork:

  - A TRUE PERSPECTIVE CAMERA CANNOT REPRODUCE BOTH. It would have to give a fighter at the far
    zboundary a different parallax rate from one at the near, and the game gives them the same.
    Anything built on it disagrees with the game's own picture the moment a fighter walks
    upstage -- which is every match.
  - So option A stands, and not merely as the conservative choice: orthographic, vertical shear
    of slope 1 by depth, horizontal translation by camera/parallax_depth. It reproduces every
    existing layer AND every object exactly, which makes the acceptance test exact too.
  - Hand-woven solids therefore get parallax, a floor that recedes at slope 1, and lighting --
    but no vanishing point. That is the HD2D look rather than a compromise toward it.

WHAT A VERTEX NEEDS, and this is the part the fork was hiding: FOUR numbers, not three. x, jump
height, floor row and parallax depth are independent in LF2, because the game never unified
them. A set authored as if depth were one axis would be authored against a camera the game does
not have. The mesh vertex format gains a parallax-depth channel; the header must say why, or
the next person will "simplify" it back to three and reintroduce the inconsistency.

None of this needed a run: it is three recorded claims read together. Worth noting because the
previous note called it "bounded RE rather than an open question" and it turned out to be
neither -- it was already answered and not yet assembled.

### Note (2026-08-12)
### Note (2026-08-12) -- THE PROJECTION IS BUILT, and it agrees with the game's own placement

geom_stage_project / geom_stage_clip in runtime/overrides/geom.h, transcribed into
runtime/shaders/mesh.vert, with the vertex format now carrying the four numbers the fork's
resolution named.

    screen_x = x - camera/depth        (C031: the horizontal is a 1/z translation)
    screen_y = row - jump              (C018: the depth axis projects down at slope 1)

NOT A MATRIX, and the header says why: screen_x = X - camera/d is not a linear function of
(X, d, 1). A 4x4 with a perspective divide gives X/d, not X - c/d. So the depth rides as a
per-vertex attribute and the division happens per vertex -- which is also what makes the
"four numbers" concrete rather than a note.

THE ACCEPTANCE TEST, and it is the assertion the whole pass rests on: a quad placed at a
layer's derived depth must land where geom_layer_offset -- the GAME'S OWN placement, a
different expression entirely -- puts that layer's picture. tests/test_geom.c walks seven real
layers of real stages (The Great Wall's sky, hill1, road1, road2 and road3; CUHK's sky;
Brokeback Clif's cliffs) at five camera positions including 0 and the stage's full pan. They
agree to within a pixel, which is the game's integer divide against the quad's float.

Also asserted there, because each is a thing that could silently be wrong:
  - the vertical: a fighter at zboundary 300 draws on row 300, at 510 on row 510, and jumping
    40 lifts it exactly 40 rows and nothing else;
  - THE PROPERTY THAT MAKES IT NOT A PERSPECTIVE CAMERA -- two points at the same depth and
    different rows shift horizontally by the SAME amount. A perspective camera cannot, which is
    why one is not used;
  - the clip-space depth ordering across the whole range the shipped stages use (0.89 to 535),
    monotone and inside [0,1], with the fighters' plane at exactly 0.5;
  - an UNKNOWN depth (0) going to the far plane and not moving with the camera at all.

Mutants run: ignoring the depth in the shift fails 24 checks; disabling the pipeline's depth
test still makes tools/e2e.sh mesh print FAIL after the rewrite, so the discriminator survived
the change to the vertex format.

WHAT IS LEFT before a stage can be woven: the pass has no TEXTURE support (vertex colour only),
so the existing bg.dat layers cannot yet be submitted as quads; and the authored-data format
does not exist. Neither is blocked on anything now.

### Note (2026-08-12)
### Note (2026-08-12) -- the texture half is de-risked too: the pass can share the display list's

Before writing texture support into mesh.c, the same question C029/C030 answered for targets was
asked for SOURCES, and measured against both classes (claim C032):

    gpu       renderer=gpu ordinary-texture GPU handle=READABLE
    software  renderer=software ordinary-texture GPU handle=absent

SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER is readable on an ORDINARY SDL_Texture -- one made with
plain SDL_CreateTexture and filled with SDL_UpdateTexture, which is exactly what render.c
already does for every sprite and layer the display list draws. That is the reverse of C030,
which covers a texture wrapped around a GPU texture the caller made.

SO THE STAGE'S ART EXISTS ONCE. The mesh pass binds the handle behind the texture render.c
already uploaded and samples it; there is no second copy of any bitmap on the GPU and no second
upload path to keep in step with the colour-key-to-alpha conversion.

ONE HAZARD, recorded in the claim's falsifier rather than discovered later: SDL may reallocate
the storage on an update or a format change, so the handle must be READ AT BIND TIME and never
cached across frames. The spike read it once and did not re-read after a second update, so
"stale handle" is unmeasured and is the first thing to check when texture support is written.

That leaves the authored-data format as the only piece of #62 with nothing measured under it.

### Note (2026-08-12)
### Note (2026-08-12) -- TEXTURE SUPPORT LANDED, and the self-test falsified a claim on its way

The pass samples art now, so the last engineering piece before authored geometry is done. The
route asserts three more lines and `ctest shaders` covers the two new blobs.

WHAT THE SELF-TEST FOUND, which is the part worth recording. Its first run came back
rgba(0,0,0,0) -- and that is the SAME answer for three different faults: the sample failed, the
UVs are wrong, or the quad never rasterised at all. So an untextured CONTROL was added, the same
quad with no art, which must read white. It reads white. That separated them in one run, and
then a second control -- a texture the pass UPLOADS ITSELF, sampled by the same pipeline with
the same UVs -- came back with its two halves correct.

So the sampler, the UVs, the pipeline and the geometry are all right, and what does not work is
BORROWING SDL'S TEXTURE. Claim C032 is falsified: SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER reads
back a non-null handle for an ordinary SDL_Texture, and a sample through it from this pass's own
command buffer returns zeros. Two hypotheses were tried and neither is it -- SDL_FlushRenderer
before the read changes nothing, and drawing the source through SDL_Render first changes
nothing, so it is not an upload waiting on a flush.

CONSEQUENCE, and it is a real cost stated plainly: the pass owns its uploads (mesh_upload /
mesh_texture_free), so a stage's art will be on the GPU twice -- once for the display list and
once for the geometry pass. That is not the design anyone would choose. It is the one that
works, and the alternative was measured rather than assumed.

The upload WAITS ON ITS FENCE before returning, because the next thing a caller does is draw
with it and an upload still in flight samples as exactly the zeros this redesign came from.

The sampler is NEAREST, like everything else the port draws (issue #41): the art is pixel art
and a linear filter on a magnified texel is the blur that removing the whole-frame scale was
about.

STILL LEFT for a stage to be woven: the authored-data format, and nothing else in the renderer.

### Note (2026-08-12)
### Note (2026-08-12) -- THE AUTHORED-DATA FORMAT EXISTS, and it is documented and tested

docs/stage-geometry.md is what an author reads. runtime/video/stagegeom.c loads it. ctest
stagegeom walks it offline in a millisecond.

THE SHAPE, and the one decision in it worth explaining: `stages/<name>.stage` keyed on the
stage's own bg.dat name, naming ordinary Wavefront OBJ models. An OBJ carries three axes and
this engine needs four -- so how?

  DEPTH IS A PROPERTY OF THE SOLID, NOT OF THE VERTEX. A pillar standing in a stage is at ONE
  parallax depth; its vertices differ in x, jump and row. So the OBJ's `v x y z` become x, jump
  and row, and the solid's own `depth:` line supplies the fourth. That is what lets any
  modeller author for this without knowing anything about LF2's projection.

  The limitation is written down rather than worked around: a solid that genuinely spans
  parallax depths -- a wall receding from the fighters' plane into the distance -- has to be
  split into several solids. That is LF2's projection, not the format's.

`depth: layer hill1.bmp` is preferred to a number and takes the depth from the stage's OWN
layer (C031), so an authored solid sits exactly in the plane of the art it belongs with and
moves with it if the data ever says otherwise. Two cases have no derivable depth and the loader
REFUSES rather than guessing: a stage that never pans, and a layer whose span is 794 or less.

WHAT THE TEST IS ACTUALLY FOR. Every failure mode of a data loader is silent -- a missing file
reads as "this stage has no geometry", an unknown key as a solid that never appears, an
unresolvable layer as a solid at the wrong depth, a skipped OBJ line as a hole in the model.
None crash and none look like a bug in a screenshot. So most of the 35 checks assert that the
loader REFUSES, with a message naming the line: seven bad fixtures, each rejected for its own
reason. The count of OBJ lines the subset does not read is reported rather than hidden, and the
fixture deliberately contains four of them per model so that count is exercised.

Two mutants run, because a test of refusals has to be shown to catch acceptance: silently
ignoring an unknown key fails 2 checks, and defaulting an unresolvable layer to the fighters'
plane fails 4.

ONE FIXTURE WAS WRONG AND THE TEST CAUGHT IT: the no-derivable-depth case first used The Great
Wall's real `sky`, span 800, which gives a six-pixel scroll range and a depth of 267 -- very far
away and perfectly derivable. The loader was right and the fixture was not; it now uses a span
of 794, where the range is genuinely zero. The comment says so, because the next person will
reach for a real sky too.

LEFT: wiring the loader to the pass (background.c knows the stage and the registry, so it is
where the lookup and the submit belong), and then the art, which is the hand-weaving itself.

### Note (2026-08-12)
### Note (2026-08-12) -- the loader is WIRED, and the insertion-order question is what is left

background.c now resolves `depth: layer <file>` against the loaded stage's own layers and
loads a stage's `.stage` when the background index changes. Proven inside the running game:
`tools/e2e.sh stage_geom`, three arms, all green.

WHERE THE FILES LIVE, and why it took a decision. The port's cwd is the GAME TREE, because the
game opens all of its own data by relative path -- and the game tree is neither in this repo nor
shipped by it. Authored geometry is the PORT's content, committed here. So `stages/` is copied
next to the binary by CMake and looked for there first, with the working directory second so a
drop-in into an existing game tree also works. No absolute path is baked in anywhere.

That copy is a custom TARGET and not a POST_BUILD command, and the difference is not cosmetic:
POST_BUILD only fires when the target relinks, so an author who adds a .stage and rebuilds would
get nothing copied and a game reporting no geometry -- the exact silent failure this subsystem
is careful about everywhere else.

WHY THE ROUTE EXISTS when ctest already walks the loader offline. Three things only exist once
the game is running, and every one of them fails SILENTLY into a game that draws exactly as it
did before -- which is also what success looks like on a stage nobody has woven:

  - is `stages/` found at all (a copy step that did nothing looks identical)
  - is the stage identified by its own name (that comes from the record, C033)
  - does `depth: layer <file>` resolve against the LOADED stage's layers

So the route asserts all three states. `present` loads at the layer's DERIVED depth and the
depth is asserted, not the vertex count alone -- a solid at the wrong depth parses, counts and
is still wrong. Brokeback Clif's `bc1.bmp` spans 1379 on a 1500 stage, so (1500-794)/(1379-794)
= 1.2068, written into the test rather than copied from its output, and that is what came back.
`bad` names a layer the stage does not have and must be REFUSED -- the arm that proves the
lookup reads the real record, since a lookup returning a constant would pass `present` whenever
the constant happened to be right. `absent` must say so and name where it looked.

THE FIXTURE IS NOT SHIPPED. It is written into the build directory for the run and removed
after. A committed .stage would put a stray solid in front of every player on that stage, which
is the author's call and not a test's. `stages/` is in the repo with a README and nothing else,
and the README says the emptiness is the honest state.

WHAT IS LEFT, and it is a design question rather than plumbing: SUBMITTING the vertices.
`mesh_draw` returns one composited texture, and one texture goes into the painter order at ONE
point -- but a set spans parallax depths and the game paints its own layers BETWEEN those
depths. The Great Wall's `road3` is in front of the fighters while its `sky` is 267 deep, so
"behind all layers" and "in front of all layers" are both wrong for a set that has a far pillar
and a near railing.

The shape that is actually right: a solid belongs immediately before the first layer whose
derived depth is <= its own, which is the game's own painter order extended to authored
geometry -- so the pass runs once per OCCUPIED gap, not once. That needs mesh.c to hold more
than one live target (its contract today is "valid until the next call"). The number of gaps a
hand-woven stage actually uses is small, so this is bounded, not a fan-out.

Do NOT settle for a single insertion point. It would look right on the first authored stage --
whichever one happens to have all its solids on one side of the layers -- and be wrong the
moment a set has a foreground and a background piece, which is what a set IS.

### Note (2026-08-12)
### Note (2026-08-12) -- the geometry is SUBMITTED, once per occupied gap. The engineering is done.

The insertion-order question the previous note left open is answered and implemented, not
deferred. THE RULE is the game's own painter order extended to authored geometry: a solid at
parallax depth d is drawn immediately before the FIRST layer whose derived depth is <= d --
after everything it is behind, before the first thing it is in front of. A solid nearer than
every layer goes after all of them, still behind the sprites, which the game places itself.

A layer's depth is derived (C031), and geom_layer_depth returns 0 where it is not derivable --
a stage that never pans, a layer that never moves. That 0 means INFINITELY FAR here and is
compared as such. Reading it as a small number would put the sky in front of the fighters.

WHAT HAD TO CHANGE for it:

  mesh.c    one target per SLOT, 8 of them, allocated on demand. A finished pass is composited
            as one quad and one quad enters the order at one point, so several gaps need
            several live targets -- a single target would be overwritten by the next gap before
            the frame was drawn. A slot out of range is REPORTED and refused, never clamped:
            clamping draws the solid at another gap's point in the order, which looks like a
            badly authored set rather than like a pass out of slots.
  render.c  E_MESH, an entry holding an already-rendered texture drawn at THIS point in the
            list. Blended, because the pass clears to transparent and every texel its geometry
            does not cover has to let the game's layers through. Not lit again by hd2d -- the
            pass shades from the same key light and the same vector, so lighting it twice is
            the only thing that could make a solid's shading and a fighter's shadow disagree.
  ddraw.c   frame_source_pixels(), the composition surface, discovered from the game's own copy
            to the primary rather than hardcoded. It is 0 until that copy has happened, and a
            frame that drops geometry for want of it is COUNTED -- "the surface is not known
            yet" and "there was nothing to draw" produce the same empty frame.

MEASURED, and the measurement is the point. `tools/e2e.sh stage_geom` gained a GPU arm with TWO
solids straddling Brokeback Clif's layers: one at bc1.bmp's derived 1.2068 (equal to layers
0..2, so it belongs behind every layer) and one at 0.5 (nearer than bc4/bc5 at 1.0, so it
belongs after all of them). Result: 1464 geometry passes over 732 frames -- EXACTLY TWO A FRAME
-- and mesh=0 on the control run with no .stage file. The override's own counter and the
renderer's agree.

One solid could not have shown this: with one solid every placement rule agrees, which is why
the arm uses two and why "one pass a frame" is called out in the failure text as the merge that
the per-gap design exists to prevent.

TWO INSTRUMENT BUGS FOUND AND FIXED IN THE TEST ITSELF, both of the same shape -- reading a
counter before the thing it counts exists:
  - LF2_QUIT_AFTER=1400 with reports on a 900-frame cadence meant the run reported the state at
    frame 900, before the match starts at ~1000. It read as "not one pass reached the frame".
    Now 1900, so the report at 1800 sees the match.
  - the assertion grepped the FIRST report line, which is the same pre-match zero. Now the last.

The byte-identity arm of `tools/e2e.sh background` still holds, ctest 11/11, and the mesh
self-test still passes. GPU: gpuguard latch clear before and after, 0 kernel trouble lines.

WHAT IS LEFT IS THE ART, and only the art. No engineering piece is outstanding. `stages/` is in
the repo with a README and nothing in it, and that emptiness is honest: the port loads what is
there and a stage with no file draws exactly as it always has.

### Note (2026-08-12)
### Note (2026-08-12) -- the set was lit from a direction no shadow in the picture came from

Found by checking a comment instead of believing it. mesh.c's header said of its key light
"These are the same numbers" as hd2d.c's. They were not:

    mesh.c   { -0.45, 0.80, 0.40 }
    hd2d.c   { -0.25, 0.94, 0.22 }

about fifteen degrees apart. So a hand-woven set would have been shaded from one direction while
every fighter and every cast shadow in the same frame came from another -- which is precisely the
contradiction the comment claims the arrangement exists to prevent.

WORSE THAN A STALE CONSTANT: hd2d.c's is not a constant at all. The pause menu's Options screen
sets it from two angles (issue #37), so a player moving the light moved the fighters and their
shadows and left the set behind. A copy could never have been right.

NOTHING COULD HAVE CAUGHT IT. The light lived inside a file that needs a GPU to run, so the only
available instrument was a screenshot -- and a set lit fifteen degrees wrong looks like a set.
That is the actual defect; the wrong numbers are a symptom of the light having no home where
anything could assert about it.

THE FIX is structural, not a corrected constant. The arithmetic moved to
runtime/video/stagelight.h -- a pure header, the same shape as geom.h -- and the shipping code
INCLUDES it. hd2d.c's angle clamp, azimuth wrap, angles-to-vector conversion and shadow shear
are all the header's now. mesh.c reads hd2d_light_vector() per draw and keeps nothing.

A SECOND COPY WAS FOUND WHILE DOING IT. hd2d.c's initialiser held { -0.25, 0.94, 0.22 } beside
default angles of (-48.7, 70), which produce (-0.2569, 0.9397, 0.2257) -- a ROUNDED copy of its
own derivation, which is the worst kind: near enough that nobody would look twice, and with
nothing anywhere that would notice if the angles moved and it did not. The default is now the
angles and the vector is derived lazily (every reader goes through light_ensure(), including the
lighting pass, which reads LIGHT directly and would otherwise have got (0,0,0) if it ran first).

ctest stagelight: 46 checks, and they are relations rather than numbers copied out of a run --
the shadow length is asserted as cot(elevation) across the range, not as 0.3639 at one angle.
Two mutants, 2 failing checks each: flipping the sign of the shadow's across term fails both
azimuth-direction assertions, and removing the elevation clamp fails both above-the-floor ones.

ONE COMMENT WAS WRONG AND THE TEST CAUGHT IT, the other way round from usual: the header claimed
the azimuth wrapped into the half-open (-180, 180], and it is the closed [-180, 180] -- -540
comes back as -180, not +180. Both name the same direction, so nothing downstream can tell them
apart. The comment was corrected rather than the code, and the test now asserts both the real
behaviour and that the two ends are the same light, so nobody "fixes" what was never wrong.

Instrument I015. tools/e2e.sh render still passes, including the arm that asserts the light
changes NOTHING on a frame with no fighters in it.

### Resolution (2026-08-13)
Authored original low-poly 3D scenes for all twelve shipped Stage Mode backgrounds in stages/, anchored to each stages own parallax layer where derivable; HK Coliseum explicitly uses the fighters plane because its 794-wide stage cannot reveal a layer depth. ctest stage_assets now loads and validates the complete shipping corpus (12/12), while the guarded stage_geom runtime route confirmed the live loader and engine pass.
