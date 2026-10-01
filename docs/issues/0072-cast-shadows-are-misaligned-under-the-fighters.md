---
id: 72
title: Cast shadows are misaligned under the fighters
status: closed
symptom: The cast shadow does not line up with the fighter it belongs to (exact misalignment measured against a frame dump)
tags: reported,rendering,shadows
created: 2026-08-16
updated: 2026-09-09
---

## What is already settled (do not re-derive)
- Issues #35 and #38 established the light-vector shear. Issue #97 corrected its anchor:
  each source point is projected by its height above object z, without reconstructing x from
  an ellipse centre. The production formula and its off-centre/wide-silhouette falsifiers are
  shared in `runtime/video/stagelight.h` and `tests/test_stagelight.c`.
- Ground-marker adjacency is no longer caster ownership. Issue #98 required independently
  authored world-object metadata so a carried physical object can cast even when LF2 suppresses
  its ellipse. The marker remains evidence for replacement and floor-band diagnostics.
