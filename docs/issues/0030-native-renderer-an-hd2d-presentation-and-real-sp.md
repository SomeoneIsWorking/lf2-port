---
id: 30
title: Native renderer: an HD2D presentation and real sprite-cast shadows
status: open
symptom: the port draws exactly what LF2 drew -- flat blits onto a software surface with the game's own fake shadow ellipse. Wanted: a native renderer with an HD2D look (depth, lighting, bloom/DOF over the pixel art) and shadows actually cast from the sprites
tags: reported,rendering,renderer,hd2d,shadows,feature
created: 2026-08-06
updated: 2026-08-06
---

REPORTED 2026-08-06. Filed on receipt, not yet scoped -- what follows is what the port
already knows that bears on it, so the first session on this does not re-derive it.

WHAT IS ASKED FOR, as three separable pieces. They are listed apart because they have very
different prerequisites and the first is a hard dependency of the other two:

  1. A NATIVE RENDERER. Today runtime/ddraw.c composes into a software surface and hands it
     to SDL3. Every sprite the game draws arrives as a Blt with a source rect, a destination
     rect and an optional colour key. An HD2D pass needs those as textured quads with depth,
     not as pixels already flattened into one buffer.
  2. HD2D PRESENTATION -- depth of field, bloom, lighting over the pixel art.
  3. REAL SHADOWS CAST FROM THE SPRITES, replacing the game's own shadow.

WHAT THE PORT ALREADY HAS THAT THIS NEEDS:

  - THE Z AXIS IS REAL AND IS ALREADY IN THE DATA. LF2 is a 2.5D beat-em-up: every object has
    a z, bg.dat carries  per stage (e.g. Brokeback Clif 300..510), and the stage
    pass fn_0041a5a0 DEPTH-SORTS the objects before drawing. So per-sprite depth does not
    have to be invented -- it is what the game already sorts on.
  - THE BACKGROUND LAYERS ARE FULLY UNDERSTOOD as of 2026-08-06 (claim C017,
    runtime/overrides/background.c): each layer's picture, scroll span, parallax rate,
    repeat step, animation frames and y. A parallax rate IS a depth cue, so the layers come
    with the ordering an HD2D pass would want.
  - THE GAME'S OWN SHADOW is a per-stage bitmap ( with
    ), i.e. a flat ellipse blitted under the fighter. Replacing it means
    declining that draw, which is the same shape of change as declining the ad panel or the
    game's mouse cursor in text.c -- there is an existing pattern for it.

WHAT IS NOT ESTABLISHED and has to be measured before any of it is designed:

  - Whether every sprite reaches the renderer through one chokepoint. fn_0043f010 draws a
    great deal (that is why the cursor could not be found by call site) but it is NOT known
    to be the only path; surf_Blt and surf_BltFast both exist and the game also writes
    pixels through Lock. A renderer that misses one path draws a game with holes in it.
  - Whether a sprite's z is reachable AT THE DRAW. The depth sort happens in fn_0041a5a0,
    upstream of the draw, so the draw call may no longer carry it. If it does not, per-sprite
    depth has to come from the object the draw is issued for, and that link is not mapped.
  - The sprite's silhouette. A cast shadow needs the alpha of the source rect, which the
    colour key gives -- but LF2 sprites already contain baked-in shading, so a projected
    silhouette may read as double-shadowing. That is an art question, not a technical one,
    and it should be looked at on a real frame before the pipeline is built for it.

SEQUENCING, and the reason to say it now: (2) and (3) are both cheap once (1) exists and
neither is possible before it. The renderer is the whole of the work. Do not start with a
shadow pass bolted onto the software compositor -- it would be a second renderer that has to
be thrown away, and it would look like progress.

BLOCKED-ON, honestly: #23 and #28 are open against the CURRENT compositor's handling of the
background layers and the camera. Those are the same subsystem. Landing them first means the
native renderer is written against a layer model that is known-correct rather than one with
two known defects in it.

### Note (2026-08-06)
TWO THINGS FROM THE SAME DAY'S WORK THAT BEAR DIRECTLY ON THIS, 2026-08-06.

1. CLAIM C010 SAYS THE PORT HAS NO ALPHA/BLEND PATH AT ALL -- 'a per-object fade cannot be
   expressed with the game's own drawing'. Everything is an opaque or colour-keyed copy. Bloom,
   depth of field and a soft cast shadow are all blend operations, so the FIRST thing the
   native renderer has to bring is a blend stage. Note that C010 is currently flagged STALE by
   `info.py claim check` (six commits to runtime/ddraw.c since it was verified) -- re-verify
   it rather than citing it, and either way the conclusion for this issue is the same.

2. THE BACKGROUND IS NOW A CLEAN SEAM TO BUILD ON. runtime/overrides/background.c owns the
   whole layer pass, and it already has, per layer and per frame: the picture, the destination,
   the parallax offset, the repeat step, and the animation frame. A renderer that wants the
   background as textured quads with depth can take them from there instead of reconstructing
   them from the blit stream -- and a parallax RATE is a depth cue that needs no invention,
   since it is literally how far the layer moves relative to the camera.

   Issue #28 is resolved and #23 is down to one deliberate hole: the black beside a
   non-looping layer in a very wide view. That hole is left for THIS issue on purpose. The
   right fill is a lit backdrop the renderer generates, not a repeated bitmap the compositor
   invents -- so it should be designed as part of the HD2D pass rather than patched before it.

### Note (2026-08-06)
FIRST SCOPING MEASUREMENT, 2026-08-06 -- 'does every sprite reach one chokepoint?' is
answered for the in-match frame, and the answer is much better than the entry assumed.

METHOD: LF2_BLT_FRAME=2250 on a VS match at 1600x550 (Brokeback Clif), which logs every blit
composing that presented frame together with the guest return address that issued it. Each
address resolved against re/functions.tsv. 137 blits, all of them accounted for:

    105 + 22 = 127   fn_0043f010   the clip draw -- ALREADY AN OVERRIDE (text.c)
              8      fn_0043f310   109 bytes; hud.c already names it as the HUD's two bars
              1      fn_0043e940   95 bytes; not yet identified
              1      fn_00401250   50 bytes; the full-screen COLORFILL that backs the frame

So a match frame is drawn through FOUR functions, one of which the port already replaces and
two of which are under 110 bytes. That is a chokepoint, and it means a renderer can be fed
from named draw calls rather than from reconstructed blit rectangles.

WHAT THIS DOES NOT ESTABLISH, and must not be read as establishing:
  - It is ONE frame of ONE stage in ONE mode. The front end, the character-select screen and
    the other stages have not been counted. A path used only by the menu would not appear.
  - It counts blits reaching surf_Blt. surf_BltFast and direct writes through Lock are
    separate paths in runtime/ddraw.c and were NOT counted here; the frame hook does not see
    them, so their absence from this list says nothing at all about whether they are used.
    Counting all three per run is the next measurement, and it needs a counter rather than a
    per-frame dump.

THE SECOND UNKNOWN IS UNTOUCHED: whether a sprite's z is reachable at the draw. fn_0043f010's
six arguments are (x, y, clip index, picture, ?, ?) -- no depth among them, which is what the
entry feared. The depth sort happens upstream in fn_0041a5a0, so the link from a draw back to
the object it belongs to is the thing to map next.

### Note (2026-08-06)
SECOND SCOPING MEASUREMENT, 2026-08-06: IS A SPRITE'S DEPTH REACHABLE AT THE DRAW? Yes,
and by a pattern this port already uses. This was the unknown the entry called the one that
decides the architecture, so it is worth the detail.

READ OUT OF fn_0041a5a0 (2173 bytes, the stage's object pass):

  0041a5d0  walks 400 slots of the EXISTS byte array at this+4 -- the same byte hud.c
            raises for a joining player -- and collects the live indices into a local list
  0041a610  bubble-sorts that list on  [ [this + idx*4 + 0x194] + 0x18 ]
            so this+0x194 is the OBJECT POINTER TABLE and +0x18 is the SORT KEY
  0041a670  walks the sorted list with EAX = the object pointer, reading +0x8, +0x70, +0x98
  and it calls fn_0043f010 SEVEN times in the body -- the sprite kinds of one object.

So the object pointer is live at every draw in the pass, and the depth the game itself sorts
on is one dereference from it. A renderer does not have to recover depth from the blit; the
pass can hand it over.

+0x18 IS THE SORT KEY, MEASURED. That it is the Z AXIS is an inference from the game being
2.5D and this being the depth sort -- reasonable, not verified. Verify it against a fighter's
z before building on it (LF2_COOP_TRACK already reports live object fields, so this is cheap).

THE MECHANISM TO USE, because it is already proven here rather than new: text.c's glyph hint.
fn_0043f010 draws everything, so text.c sets a hint before calling the original body and
clears it after, and the blit path reads it. The same shape gives every sprite its depth: an
override of fn_0041a5a0 latches the current object before each of the seven draws and clears
it after. No new plumbing, and it degrades safely -- a draw with no hint is simply a draw the
pass did not issue.

STILL NOT MEASURED, and it is now the only one of the three original unknowns left:
whether surf_BltFast and direct writes through Lock carry any pixels. The frame hook cannot
see them, so their absence from the earlier count says nothing. That needs a counter on all
three paths reported at exit, not a per-frame dump.

FOR THE SHADOW (piece 3 of this issue): one of those seven fn_0043f010 calls is the game's
flat shadow ellipse. Identifying WHICH is what lets the port decline it the way text.c
declines the ad notice and the game's mouse cursor. Not done.

### Note (2026-08-06)
THIRD SCOPING MEASUREMENT, 2026-08-06 -- the last of the three unknowns this entry was filed
with. LF2_DRAW_PATHS=1 counts all three routes that can carry pixels, over a full run that
reaches a match:

    draw paths: Blt=19753  BltFast=0  Lock=0 (of which changed pixels=0)

So the game draws through ONE COM method. surf_BltFast is never called and the game never
writes into a surface between Lock and Unlock -- the Lock route is counted by whether the
pixels CHANGED, not by whether a lock happened, because a lock taken to read is not a draw.

HOW MUCH THIS IS WORTH, stated rather than left to be assumed. Blt=19753 in the same report
proves the counters run, so the zeros are not a dead instrument. But BltFast's counter has
never been seen non-zero on any run, so '0' is consistent with both 'unused' and 'miscounted';
it is one line adjacent to the one that demonstrably works, which is as much confidence as is
available without a synthetic call.

WHAT THE INSTRUMENT SAYS IT CANNOT SEE, and it prints this every time rather than only when
it finds nothing: GDI text goes straight into the surface without a Lock (runtime/gdi.c), and
a lock whose writes cancelled out would hash the same. So the true answer is TWO routes:
    fn_0043f010 / fn_0043f310 / the fills, reaching surf_Blt        -- all the sprites
    runtime/gdi.c                                                   -- the game's text
Both are named, both are already hooked in this port, and neither has to be recovered from
pixels.

ALL THREE ORIGINAL UNKNOWNS ARE NOW CLOSED:
  chokepoint   yes -- a match frame's 137 blits come from four functions, 127 through
               fn_0043f010, which is already an override
  depth        yes -- fn_0041a5a0 has the object pointer live at every draw and sorts on
               object+0x18; the glyph-hint pattern in text.c is the way to carry it across
  paths        one COM method plus GDI text, measured above

WHAT THE ARCHITECTURE QUESTION IS NOW, since it is no longer 'can this be fed': it is whether
the renderer consumes a display list built in the overrides (sprite + rect + depth + key) or
replaces runtime/ddraw.c's software blit with a GPU path behind the same COM surface. The
first is the one that can be built incrementally alongside the current compositor and diffed
against it frame by frame, which is how every other piece of this port was landed safely.
Claim C010 (no alpha/blend path anywhere) is the first thing either design has to bring.

### Note (2026-08-06)
THE DEPTH FIELD IS NOW MEASURED, NOT INFERRED, 2026-08-06 -- and the port had it mislabelled.

The note above said '+0x18 IS THE SORT KEY, MEASURED. That it is the Z AXIS is an inference'.
It is now measured too, and claim C018 carries it:

  pressing RIGHT   x (+0x10) moves 815 -> 731 -> 804 -> 993;  +0x18 stays 334 throughout
  pressing UP      +0x18 moves 334 -> 300 and stays;          +0x14 never leaves 0
  +0x14 shows -6 for one sample mid-jump, which is the vertical axis and nothing else is

and independently: up walked +0x18 to exactly 300, while Brokeback Clif's bg.dat says
'zboundary: 300 510'. The stage data's own lower z bound and the field's floor are the same
number, from two sources that share nothing.

So an object is x at +0x10, y (jump height) at +0x14, z at +0x18, and fn_0041a5a0 sorts on z,
which is correct for a 2.5D game.

THE PORT WAS CALLING +0x18 'y'. LF2_COOP_TRACK printed 'x=%d y=%d' with y reading +0x18 --
harmless for the movement assertions, which only use x, but a renderer built on that label
would have depth-sorted the world on JUMP HEIGHT, and it would have looked nearly right until
someone jumped. Fixed in runtime/overrides/coop_debug.c, which now prints x, y and z with the
measurement written beside them.

This is the last thing this issue needed before design. Its inputs are known and named:
sprites and their depth from fn_0041a5a0 / fn_0043f010, text from runtime/gdi.c, the stage's
layers with their parallax rates from runtime/overrides/background.c, and z bounds per stage
from bg.dat's zboundary.

### Note (2026-08-06)
THE PRESENT PATH IS NOT A GPU PIPELINE, and the codemap said it was. Checked 2026-08-06
because a renderer design that believed it would start from the wrong place.

WHAT IS ACTUALLY THERE (runtime/ddraw.c hostwin_present, runtime/win32.c):

    SDL_CreateRenderer(window, NULL)            -- SDL's 2D renderer, driver unspecified
    every frame: the composition, already flattened by the software blitter, is memcpy'd
    row by row into ONE streaming SDL_PIXELFORMAT_XRGB8888 texture, then
    SDL_RenderClear + SDL_RenderTexture(NULL, NULL) + SDL_RenderPresent

So the GPU sees exactly one full-screen quad per frame, carrying pixels that were composed
on the CPU. There is no per-sprite geometry, no render target, no shader, and no depth
buffer anywhere in the port. The codemap's 'DirectDraw -> SDL3 GPU' was loose language for
'SDL3's 2D renderer' and has been corrected.

WHAT THAT MEANS FOR THE THREE PIECES ASKED FOR:
  bloom / DOF     need a render target and a shader pass, neither of which exists. This is
                  the smallest of the three -- it operates on the finished frame, so it does
                  not require sprites to become quads.
  per-sprite      needs the composition to stop being flattened on the CPU. That is the
  lighting/depth  large change: the display list from the overrides has to reach the GPU as
                  geometry instead of feeding the software blitter.
  cast shadows    needs the sprite's alpha (the colour key gives it) AND a blend stage.
                  Claim C010 says the port has no blend path at all.

SO THE ORDER IS FORCED, and it is the opposite of the order the pieces were asked in: a
render target and a blend stage first, because they are what every other piece needs; then
sprites as geometry with the depth from C018; then the look. Anything built before the blend
stage exists would be built on the software blitter and thrown away.

### Note (2026-08-06)
DELIVERED 2026-08-06 -- the renderer, the HD2D pass and the sprite-cast shadows are all in.

  NATIVE RENDERER   runtime/render.c. The game's draws become a display list, recorded per
                    destination surface; the game composes off-screen and copies to the
                    primary, so the source of that copy NAMES the composition and the frame
                    boundary is found from the game's own blits with nothing hardcoded.
                    Drawn as GPU quads into a render target AT THE WINDOW'S RESOLUTION --
                    measured drawing 978x550 of game at 1920x1079. GDI text, which never goes
                    through Blt, rides as premultiplied tiles at their real place in the list.
  BLEND STAGE       the colour key becomes ALPHA on upload. This is what claim C010 said the
                    port did not have and could not do without; everything below needs it.
  HD2D              bloom with no shader toolchain: the bright pass is SDL_BLENDMODE_MOD of
                    the frame over itself (squaring in 0..1 is a soft threshold), two linear
                    downsamples are the blur, then additive. SDL 3.4 can take SPIR-V/DXIL/MSL
                    through SDL_GPURenderState, but that is a shader compiler in a build that
                    otherwise needs only a C compiler and SDL.
  CAST SHADOWS      the sprite's own silhouette, sheared onto the ground through
                    SDL_RenderGeometry -- SDL_RenderTexture cannot shear, and without the
                    shear a squashed sprite reads as a reflection. Vertex colour is black with
                    alpha, so SDL's multiply leaves the sprite's alpha and zeroes its colour:
                    the silhouette exactly. WHERE it goes is the game's own answer -- the
                    ellipse it drew is at the object's feet, so its centre and bottom edge are
                    the ground point, and the sprite that follows is the object it belongs to.

WHAT WAS REPLACED AND WHY IT MATTERED: the game's shadow is one flat DITHERED ellipse per
object. At 794x550 it reads as soft; at 1920 it is a visible checkerboard, which is what
rendering at the window's resolution exposed. Claim C019 carries the identification, including
the wrong answer that came first -- a pointer at record offset -1128 that looks like a shadow
handle and matched 0 of 40000 clip draws, because the records are contiguous and it belongs to
the previous background.

VERIFIED, ctest render, four arms: with the effect off the GPU frame matches the software
compositor to a MAX of 1-2 levels of 255 (antialiased glyph edges only); dropping every 7th
draw changes 134928 px, so the match can fail; the HD2D arm differs. 2778 ground markers
produced 2778 cast shadows in one run -- every marker consumed.

STILL OPEN on this issue, and not pretended otherwise:
  - depth-sorted LIGHTING. The depth is available (C018, object+0x18) and the object pass is
    the place to latch it, but nothing uses it yet: the shadows are placed from the game's own
    ellipse rather than from z.
  - tilt-shift / DOF, the other half of the HD2D look.
  - GDI text is still rendered at the composition's resolution and scaled up with everything
    else. It is a TTF glyph and could be rendered at output resolution; the tile mechanism
    already carries an arbitrary-sized bitmap, so this is small.
  - the pause menu and the controls hint still present through the software compositor,
    because they draw straight onto the primary and are in no display list.

### Note (2026-08-06)
NARROWED AND DELIVERED 2026-08-06, after a first attempt that was rejected on sight.

WHAT WAS WRONG WITH THE FIRST ATTEMPT, because it is the useful half of this note. It read
"HD2D presentation" as the full Octopath post chain and shipped bloom, depth of field,
atmospheric haze, a vignette and a colour grade on top of the lighting. Every one of those
touches every pixel of the frame. Together they did not read as light in a scene; they read
as a filter over a screenshot -- the game came out foggy, washed out and blurred, and the
verdict was "you just spammed all sorts of effects". The bright pass was also not a bright
pass: with no shader path it approximated a threshold with SDL_BLENDMODE_MOD of the frame
over itself, which is squaring, so every midtone still contributed and the glow was uniform.

THE SCOPE THAT WAS ACTUALLY WANTED, and what shipped:

  ISOMETRIC LIGHTING, ON THE STAGE'S CHARACTERS ONLY. One key light given as a direction in
  the stage's own three axes -- x across, y up (LF2's jump axis, C018), z toward the camera --
  plus a hemisphere ambient. Normals come from the gradient of the sprite's own silhouette,
  which bevels the edge of a fighter and leaves the middle facing the camera; the interior
  shading stays the artist's, which is the right answer for pixel art. It is applied ONLY
  where the character mask is set. The background layers, the HUD, the text and the letterbox
  come out as the exact pixels the game composed.

  CAST SHADOWS. The sprite's own silhouette laid on the ground and sheared along the SAME
  light vector -- there is no second constant anywhere for a shadow direction, so the shading
  and the shadows cannot disagree.

WHICH DRAWS ARE CHARACTERS is the game's own answer and cost nothing: LF2 draws the stage's
shadow ellipse at an object's feet immediately before drawing the object, so a sprite with a
ground marker in front of it is an object in the field. That one fact supplies the mask, the
ground point for the shadow, and the height-above-ground channel, and it is why the whole
placement-hint mechanism the first attempt grew (per-layer parallax depth, a UI hint stack
threaded through background.c, hud.c and text.c) could be deleted outright.

THREE THINGS THAT WENT WRONG IN THE BUILDING, all worth not repeating:

  THE DEBUG VIEW LIED. LF2_HD2D_SHOW looked its buffers up at the END of the chain, by which
  time the half-resolution scratch that had held the shadow mask had been reused by a later
  blur -- so SHOW=shadow displayed a blurred copy of the whole scene and read as "the shadow
  mask contains the entire picture". It now shows each buffer at the moment that buffer is
  final. A debug view that shows the wrong buffer is worse than none: it answers confidently.

  THE SHADOW MASK CARRIED THE FIGHTER'S COLOURS. SDL multiplies the vertex colour into the
  texture, so a white vertex gives sprite.rgb * a, not coverage; the lighting then read the
  teal jacket as "how much shadow" and the shadow was darker under the bright parts of the
  sprite. No fixed-function blend factor substitutes 1 for the source colour, so the mask
  needs its own fragment shader (hd2d_shadow.frag). Alpha-tested, not blended, so two
  fighters standing together do not throw a doubly dark shadow.

  THE BEVEL WAS A BLACK OUTLINE. The raw Sobel of a binary mask is 4 at a step edge, which
  tips the normal almost into the screen plane, and a hard max(dot,0) then sent one whole side
  of every fighter to ambient only. Divided by its own maximum, and wrapped (dot*0.5+0.5,
  squared) so the shaded side still sees the sky.

INFRASTRUCTURE THIS NEEDED. SDL's default renderer order picks the OpenGL backend, which has
no SDL_GPUDevice and therefore no SDL_GPURenderState -- no shaders at all. The renderer is now
created as SDL_GPU_RENDERER by name, with a checked fallback. Shaders are committed SPIR-V
(tools/build_shaders.sh; ctest shaders recompiles and compares wherever glslc exists, proven
to fail on a stale blob), so the build still needs only a C compiler and SDL. A backend that
cannot take SPIR-V is told about on stderr and gets the plain composition -- deliberately no
approximation to fall back on, since that is exactly how the fake bloom survived.

VERIFIED, ctest render, four arms: the GPU frame matches the software compositor to max 1-2
levels of 255 on antialiased glyph edges only; dropping every 7th draw changes 134928 px, so
the match can fail; the light changes 8613 px on the MATCH frame; and the light changes ZERO
pixels on the character-select frame. That last arm is the one worth having -- "the effect
ran" is satisfied by an effect that has spread over the whole frame, which is precisely the
failure that shipped the first time.

STILL OPEN ON THIS ISSUE:
  - MAKING THE STAGES THEMSELVES 3D. Asked for; not done, and not for want of trying to see
    how. A stage is N painted 2D layers plus a floor, and the only depth information in the
    data is each layer's parallax rate. That rate is 1/depth exactly -- and LF2's own linear
    parallax formula is already the correct projection of a billboard at that depth for a
    camera that only pans horizontally, which is the only camera the game has. So putting the
    layers in a perspective frustum at their measured depths reproduces the picture the game
    already draws, pixel for pixel, and buys nothing. Anything beyond that -- tilting the
    floor into a receding plane, extruding the cliffs, a camera that dollies -- means
    inventing geometry the stage does not have, on art that is already painted in
    perspective. Two things WOULD be real and are the honest next step if this is wanted:
    (a) light the floor as a GROUND PLANE rather than as a billboard, which is real 3D
    shading from geometry that genuinely exists (the floor is horizontal); this needs the
    floor layers identified, which is not done. (b) let fighters occlude and be occluded by
    scenery by z rather than by draw order -- but the game already depth-sorts, so this too
    would change nothing visible.
  - the floor is lit as a billboard, per the above.
  - GDI text is still rendered at the composition's resolution and scaled up with everything
    else. It is a TTF glyph and could be rendered at output resolution; the tile mechanism
    already carries an arbitrary-sized bitmap, so this is small.
  - the pause menu and the controls hint still present through the software compositor,
    because they draw straight onto the primary and are in no display list.
