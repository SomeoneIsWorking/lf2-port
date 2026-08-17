---
id: 72
title: Cast shadows are misaligned under the fighters
status: resolved
symptom: The cast shadow does not line up with the fighter it belongs to (exact misalignment measured against a frame dump)
tags: reported,rendering,shadows
created: 2026-08-16
updated: 2026-08-17
---

## Reported

The user asked to 'fix shadow alignment'. The cast shadow was re-anchored when the lighting
moved into the engine (issue #69): light_emit_shadow in runtime/video/engine.c anchored the
shadow's horizontal position at the OBJECT'S OWN BASE (q.x + q.w/2) rather than the ground
ellipse's centre, on the reasoning that 'a shadow must stand under the character'. That
re-anchoring is what this issue measured and fixed.

## Characterised, against frame dumps of the same deterministic frame (match+120, run

PAD="south@frontend+0,...,right@match+108,south@match+158" with LF2_UNPACED=1,
LF2_FRAME_DUMP='@match+120', dump dir scratch/shadow_car/):

Measurements are in composition pixels (794x550, world scale 1), from the SHOW=shadow and
SHOW=chars masks plus the LF2_HD2D=off frame (which draws the game's own shadow ellipse):

| fighter | sprite-quad centre (chars mask) | game ellipse centre (ellipse in the off frame) | shadow foot-centre BEFORE | AFTER |
|---|---|---|---|---|
| left (P1, grounded)  | 233.5  | 224.5  | 231.5  | 226.5  |
| right (CPU, airborne, lift 24 px) | 315.5 | 311.5 | 323.5 | 320.0 |

- The misalignment is horizontal, in the +x direction (shadow to the RIGHT of the fighter's
  feet), and VARIABLE PER FRAME: 7 px vs 12 px on this one frame, because it is the sprite
  frame's internal offset (its rectangle's centre vs its feet) that the sprite-quad anchor
  absorbed. That is why it read as "does not line up" rather than "shifted by a constant".
- The vertical placement was already correct: the shadow's foot row sat at the ellipse's
  bottom edge (ground_gy), and the airborne displacement followed h*(across, -up) exactly
  (the right fighter's foot landed at 315.5 + 24*0.273 = 322.1, measured 323.5).
- The ground truth for "where the fighter stands" is the game's own ellipse centre, drawn at
  obj.x + obj.dx - cam (objects.c) -- the old draw_cast_shadow anchored there, the engine did
  not.

## The fix (three edits, two of them blocking)

1. **The lighting chain was dead since issue #69.** `light_ok` was computed
   (`sh_light_vert && p_chars && p_shadow && p_light && smp_linear`) BEFORE `smp_linear` was
   created, so it was always 0 and the whole chain reported "did not come up" on every
   backend. The engine drew unlit frames with the game's own ellipse DELETED (a marker), so
   fighters had NO cast shadow at all -- the engine.c:567/579 ordering was the first thing to
   fix before any shadow could even render or be measured. This also means the e2e render
   route's light arm (match frame must change, menu frame byte-identical) had been red since
   #69.
2. **LF2_HD2D_SHOW=shadow returned the character mask.** The final return of lighting_passes
   was `want_light ? tex_lit : (want_chars ? tex_chars : tex_shadow)`, and `want_chars` is
   true for SHOW=shadow too (a shadow needs the character mask to shade against), so the
   diagnostic could never show a cast shadow. It now returns `show == 2 ? tex_shadow : tex_chars`.
3. **The anchor.** EngineQuad gained `ground_cx` (the ellipse's horizontal centre), filled in
   render.c from the ground marker rect, and light_emit_shadow uses it as the shadow's anchor
   instead of q.x + q.w/2. The projection arithmetic in stagelight.h is unchanged; only the
   ground point passed to it changed.

## What is already settled (do not re-derive)

- Issues #35 (airborne offset) and #38 (shadow shape follows the light) are resolved: the
  projection is h * (across, -up), the foot edge sits at the ground point, the head edge is
  the foot edge plus the full-height shear. All of it is shared arithmetic in
  runtime/video/stagelight.h (stagelight_shadow_quad), with offline checks in
  tests/test_stagelight.c. The arithmetic was NOT changed by this fix -- only the cx the
  engine passes (the ellipse centre rather than the sprite quad centre).
- The pairing of a shadow marker with its sprite is checked against the game's own geometry
  (the ellipse is drawn AT the object's feet), and 102 orphaned markers per run are discarded
  and counted (issue #35).

## Verified

- ctest 13/13 in scratch/build.
- The lit frame after the fix differs from before ONLY in the two shadow regions
  (207 px, bbox x 208..358 y 315..339): the shadows moved onto the fighters' feet, nothing
  else moved.
- Shadow foot-centres after the fix (above table) sit on the ellipse centres (within ~2 px,
  which is the silhouette's own feet pixels, not the anchor).
