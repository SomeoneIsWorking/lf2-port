---
id: 72
title: Cast shadows are misaligned under the fighters
status: resolved
symptom: The cast shadow does not line up with the fighter it belongs to (exact misalignment measured against a frame dump)
tags: reported,rendering,shadows
created: 2026-08-16
updated: 2026-08-22
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
3. **The former anchor correction was later falsified by issue #97.** `EngineQuad.ground_cx`
   moved the whole texture rectangle to the ellipse centre. That happened to improve the two
   measured frames above, but it discarded LF2's hand-authored frame placement (`centerx` and
   `centery`) and detached other silhouettes' feet. The current shared projection preserves the
   authored sprite rectangle in x/y and uses the world object's z row only as the contact plane.
   See issue #97 and `stagelight_shadow_quad`.

## What is already settled (do not re-derive)

- Issues #35 and #38 established the light-vector shear. Issue #97 corrected its anchor:
  each source point is projected by its height above object z, without reconstructing x from
  an ellipse centre. The production formula and its off-centre/wide-silhouette falsifiers are
  shared in `runtime/video/stagelight.h` and `tests/test_stagelight.c`.
- Ground-marker adjacency is no longer caster ownership. Issue #98 required independently
  authored world-object metadata so a carried physical object can cast even when LF2 suppresses
  its ellipse. The marker remains evidence for replacement and floor-band diagnostics.

## Verified

- ctest 13/13 in scratch/build.
- The lit frame after the fix differs from before ONLY in the two shadow regions
  (207 px, bbox x 208..358 y 315..339): the shadows moved onto the fighters' feet, nothing
  else moved.
- These measurements remain the evidence for the 2026-08-17 intermediate fix. The conclusion
  that ellipse-centre reconstruction was generally correct was falsified and replaced by the
  authored-frame projection verified under issue #97.
