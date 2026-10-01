---
id: 76
title: Backgrounds jitter while scrolling
status: closed
symptom: Stage background layers visibly jitter as the camera scrolls instead of moving stably with their authored parallax. The cause must be located in camera/parallax coordinates or renderer sampling; a magic offset, per-stage special case, or hiding the motion is not an acceptable fix.
tags: reported,renderer,background,scrolling,parallax
created: 2026-08-20
updated: 2026-09-09
---

## Root cause
LF2's layer formula is rational, but the original 794x550 path applies x86 IDIV and draws only
the truncated integer coordinate. The full-output renderer reused that integer and then
magnified it independently for every layer. At a 3.591 scale, each layer therefore sat still
until its own quotient crossed an integer boundary, then caught up by one complete logical
pixel—3 to 4 output pixels—at a different frame from the other parallax planes.

## What was tried / dead ends
Composing the whole scene on the 794x550 integer grid and scaling the finished texture once
did hide the independent catch-up steps. It is not a valid fix: it discards issue #41's
full-resolution per-draw rendering, reduces fonts/SVGs/lighting before presentation, and caused
the 3840x1975 output band. Camera quantisation, temporal smoothing and stage-specific offsets
likewise hide or retime the lost precision rather than preserving it.
