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
