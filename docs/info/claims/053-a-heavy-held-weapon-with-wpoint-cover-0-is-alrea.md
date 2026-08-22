---
id: C053
kind: claim
status: holds
created: 2026-08-22
tags: rendering,re,issue-85
depends: runtime/overrides/objects.c#fn_0041a5a0, runtime/video/engine.c#lighting_passes, runtime/video/engine_visibility_probe.c#engine_visibility_probe_run, runtime/shaders/quad.frag#main
reconfirmed: 2026-08-22
verified_at: 2026-08-22 14:48:17
---

## Claim

A heavy held weapon with wpoint cover 0 is already painted in front of its carrier; the native character mask must depth-test the carrier silhouette against the completed scene painter depth or covered hair is relit through the weapon.

## Evidence

The original instruction listing at 0x00418310..0x0041849a sets the held object's z to carrier z+1 for cover 0, and fn_0041a5a0 sorts ascending z before painting. Source inspection shows the native lighting chain redraws character silhouettes after that completed colour pass, and its fragment result modulates the already-painted albedo. tests/test_painter_depth.c verifies only that the shared shipping ordinal mapping makes later painters nearer while remaining inside the clip range; it does not claim to test SDL's fixed-function depth state. The procedural shipping-engine GPU route verifies the fixed-function visibility chain and its transparent-fragment negative on real output, with the exact samples recorded below.

## What would falsify it

A faithful decompilation shows cover 0 does not set the held object to carrier z+1, the object pass does not paint ascending z, or a scene-depth capture shows a covered carrier fragment passing LESS_OR_EQUAL against the later weapon's depth.

## Re-confirmed 2026-08-22

The original instruction listing at 0x00418310..0x0041849a sets the held object's z to carrier z+1 for cover 0, and fn_0041a5a0 sorts ascending z before painting. The 2026-08-22 procedural shipping-engine GPU route then produced unlit #2040c0/#804020, mask #000000/#ffff00, reversed mask #ffff00/#ffff00, and lit #2040c0/#20110a. The nonuniform normal mask plus uniform reversed mask proves completed-scene painter depth rejects only the later opaque occluder, while the transparent half writes no depth; the final arm proves that visibility controls actual character relighting.
