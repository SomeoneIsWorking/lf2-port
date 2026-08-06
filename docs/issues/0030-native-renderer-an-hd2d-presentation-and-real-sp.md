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
