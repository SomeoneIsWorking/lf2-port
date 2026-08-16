---
id: 72
title: Cast shadows are misaligned under the fighters
status: open
symptom: The cast shadow does not line up with the fighter it belongs to (exact misalignment to be characterized against a frame dump)
tags: reported,rendering,shadows
created: 2026-08-16
updated: 2026-08-16
---

## Reported

The user asked to 'fix shadow alignment'. The cast shadow was re-anchored when the lighting
moved into the engine (issue #69): light_emit_shadow in runtime/video/engine.c now anchors
the shadow's horizontal position at the OBJECT'S OWN BASE (q.x + q.w/2) rather than the ground
ellipse's centre, on the reasoning that 'a shadow must stand under the character'. That
re-anchoring is the prime suspect for a NEW alignment defect.

## What is already settled (do not re-derive)

- Issues #35 (airborne offset) and #38 (shadow shape follows the light) are resolved: the
  projection is h * (across, -up), the foot edge sits at the ground point, the head edge is
  the foot edge plus the full-height shear. All of it is now shared arithmetic in
  runtime/video/stagelight.h (stagelight_shadow_quad), with offline checks in
  tests/test_stagelight.c.
- The pairing of a shadow marker with its sprite is checked against the game's own geometry
  (the ellipse is drawn AT the object's feet), and 102 orphaned markers per run are discarded
  and counted (issue #35).

## To characterize

- What does the misalignment actually look like (offset by a fixed amount? only when airborne?
  only in the engine path?), measured against an LF2_FRAME_DUMP with LF2_HD2D_SHOW=shadow and
  a same-frame picture. The mask must be compared to the sprite it is supposed to stand under.
