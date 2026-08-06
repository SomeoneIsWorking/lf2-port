---
id: 38
title: The cast shadow's SHAPE ignores the light; only its distance follows
status: resolved
symptom: reported. draw_cast_shadow lays the sprite down at a FIXED 0.30 of its height and shears it by hd2d_shadow_lean(). So moving the light changes where the shadow points and how far it leans, but never how LONG it is -- a light near the horizon should throw a long shadow and one overhead should throw almost none, and neither happens. The true projection of a point at height H under a directional light L is a ground displacement of H*(-Lx/Ly, -Lz/Ly); the port uses the first term and a constant in place of the second. The airborne offset has the same defect -- it moves the shadow sideways but not up the screen
tags: lighting
created: 2026-08-06
updated: 2026-08-06
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-06)
The shadow is a true projection now. hd2d_shadow_project() gives where a point at height 1
lands on the ground -- (-Lx/Ly, Lz/Ly) in screen units -- and draw_cast_shadow uses BOTH terms
for both things it does: the sprite's own height gives the far edge of the shadow (its LENGTH,
which was a fixed 0.30 of the sprite before and therefore ignored the light entirely), and the
object's height off the floor displaces the whole shadow when it is in the air (which
previously moved sideways only, never up the screen).

VERIFIED by putting the light somewhere known and measuring, since 'the shape follows the
light' is not something one screenshot can show. LF2_HD2D_LIGHT=<az>,<el> is a diagnostic for
exactly this. Same frame, same stage, only the elevation changed:

    elevation 20  ->  13102 shadowed floor pixels, spanning 311 rows x 405 px
    elevation 85  ->   5131 shadowed floor pixels, spanning 203 rows x 244 px

2.6x the area from a low sun, which is what cot(elevation) says it should be.
