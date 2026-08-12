---
id: 62
title: Stage mode: hand-weave the levels in 3D so HD2D has real depth to light
status: open
symptom: Stage mode's levels are flat 2D layers, so the HD2D pass has nothing to light: the depth it needs is not in bg.dat and cannot be derived from it. The ask is per-stage 3D geometry, authored by hand, that the levels are woven into.
tags: reported,renderer,hd2d,stage
created: 2026-08-12
updated: 2026-08-12
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
