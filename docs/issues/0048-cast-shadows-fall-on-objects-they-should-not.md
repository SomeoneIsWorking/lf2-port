---
id: 48
title: Cast shadows fall on objects they should not
status: resolved
symptom: a sprite-cast shadow darkens pixels of an object it should not touch -- it displays over objects rather than only on the ground behind them
tags: reported,rendering,renderer,hd2d,shadows
created: 2026-08-11
updated: 2026-08-11
---

REPORTED 2026-08-11. Filed on receipt, not yet reproduced.

WHAT THE PORT DOES TODAY, so the next session starts from the mechanism rather than the
screenshot (runtime/video/render.c, runtime/video/hd2d.c, and claim C019):

  - An OBJECT is a sprite the game drew a shadow ellipse in front of. The game identifies them
    for free: fn_0041a5a0 draws each object's ellipse immediately before the object itself, so
    "the next sprite after a ground marker" is the rule -- with a horizontal-overlap check,
    because a sprite clipped entirely off screen never reaches the list and the marker would
    otherwise bind to the NEXT object (measured: a marker at x 988 paired with a sprite at
    x 2076).
  - PASS_SHADOW walks the SAME display list and, for each object, shears that sprite's
    silhouette along the light vector through SDL_RenderGeometry into rt_shadow -- a MASK, one
    channel of coverage. draw_cast_shadow projects the sprite's top by its own height and
    displaces the whole shadow by how far the object is off the floor.
  - hd2d_post then takes light AWAY wherever that mask says shadow, over the whole frame.

SO THE LIKELY CAUSE, stated as a hypothesis to be MEASURED and not as a finding: the mask has
no notion of depth or of which pixels belong to which object. It is one full-screen coverage
buffer, and the lighting subtracts through it everywhere -- so a shadow sheared from a fighter
standing BEHIND another object lands on that object's pixels just as readily as on the floor.
The game's own draw order is a depth sort (fn_0041a5a0 sorts before drawing), and that ordering
is thrown away the moment every shadow is flattened into one mask.

WHAT MUST BE ESTABLISHED FIRST:
  1. REPRODUCE IT AND SAY WHICH PIXELS ARE WRONG. Two fighters at different z, one behind the
     other; dump the frame with the light on and with LF2_HD2D=off and diff. LF2_SHADOW_DEBUG=1
     reports the ground identification. Without a frame that shows it, any fix is guesswork.
  2. WHETHER THE OFFENDING PIXELS ARE AN OBJECT'S OR THE FLOOR'S. The port already has a
     character mask (rt_chars, PASS_CHARS) that says which pixels are a fighter. If the fix is
     "a shadow must not darken a pixel that belongs to an object nearer the camera than the
     caster", then both the mask and a per-object depth have to reach the shader, and today
     neither does -- rt_chars is a single mask with no identity in it.
  3. WHAT THE GAME ITSELF DOES, because that is the reference. LF2's own shadow is a flat
     ellipse blitted on the floor UNDER the fighter, before it -- so it is painted over by any
     object drawn later, and depth is handled by draw order alone. The port replaced that
     ellipse with a real cast shadow and in doing so left the drawing model the game used.
     Whether the right answer is to restore draw-order semantics (shadow into the frame at the
     caster's own position in the list, rather than into one flat mask) is the design question,
     and it should be settled before any shader is touched.

DO NOT fix this by shrinking the shadow, by fading it, or by excluding a hardcoded band of the
screen. Those make the screenshot look right without anyone having found out which pixels were
wrong or why.

RELATED: issue #30 (the HD2D renderer this belongs to), issue #31 (the shadow was a blurry blob
and the character was lost in it), claim C019 (how an object is identified), issue #40 (GPU
runs on this machine are limited to ONE at a time until Vulkan validation has been run, so
reproducing this must not be done as a batch).

### Resolution (2026-08-11)
FIXED, and the fix is one token: the cast-shadow term is no longer applied to the CHARACTER
branch of runtime/shaders/hd2d_light.frag.

    before   vec3 character = albedo * (ambient + sun * (u_sun_dir.w * ndl * shade));
    after    vec3 character = albedo * (ambient + sun * (u_sun_dir.w * ndl));

THE CAUSE, and it is not the depth-sorting hypothesis this entry was filed with. The mask does
lack depth and identity, but that is not what produced the reported symptom. A shadow is
sheared from the caster's OWN silhouette starting at the caster's OWN feet, so it overlaps the
caster's lower body every frame -- and because `shade` was multiplied into the character term,
every object was standing in its own shadow. It needs no second object to reproduce.

The shader's own header already stated the rule it was breaking: "The only thing that touches
them is the CAST SHADOW, and that is the point of a cast shadow -- it falls on the floor, not
on the fighter." So does the game: LF2 blits a flat ellipse on the floor UNDER a fighter and
draws the fighter over it, so a shadow has never darkened an object's pixels in this game. The
port kept that rule for the ground and lost it for the characters.

MEASURED, run against BOTH classes rather than reasoned about. The discriminator is
LF2_HD2D_SHADOW=0 against the default, which isolates the cast-shadow term with the rest of the
lighting still on, and LF2_HD2D_SHOW=chars gives the character mask to test the pixels against.
Frame 2400 of the render route, 1920x1080:

    before the fix   1092 cast-shadow pixels, 379 of them on CHARACTER pixels
    after  the fix    713 cast-shadow pixels,   0 of them on character pixels

1092 - 713 = 379 exactly: the shadow on the floor is untouched and only the pixels on objects
went. Repeated on frames 2250/2550/2700 (381/428/271 shadow pixels, all on the ground).

A WRONG MEASUREMENT ON THE WAY, recorded because it looked convincing and was not. The first
"reproduction" diffed the frame against LF2_HD2D=off and called the difference shadow. That
switch turns off the WHOLE lighting pass, so the 1837-2245 pixels it reported were the key
light, the bevel and the floor tint as well -- most of a fighter's body lights up under it, and
the marked-up frame looked exactly like the reported bug. 1508 of those pixels were on
characters and only 620 of the shadow MASK overlapped the character mask at all, which is the
contradiction that gave it away. LF2_HD2D=off is the control for "is the pass running"; it is
not the control for any single term in it.

NOT DONE, and it is a different question from the one reported: a shadow still does not know
about depth, so nothing here stops one object's shadow falling on ANOTHER object. It cannot
today, because both would have to be excluded and the character mask has no identity in it.
That case does not arise from the current geometry -- shadows are short and start at the
caster's feet -- and no frame has shown it. If one does, it is a new issue with a frame in it,
not a reopening of this one.
