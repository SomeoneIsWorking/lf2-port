---
id: 76
title: Backgrounds jitter while scrolling
status: open
symptom: Stage background layers visibly jitter as the camera scrolls instead of moving stably with their authored parallax. The cause must be located in camera/parallax coordinates or renderer sampling; a magic offset, per-stage special case, or hiding the motion is not an acceptable fix.
tags: reported,renderer,background,scrolling,parallax
created: 2026-08-20
updated: 2026-08-22
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-20)
The jitter came from independently placing every scrolling layer at fractional output coordinates, which changed each nearest-sampling phase as the camera moved. The engine now composes the entire world on the native integer grid and scales the finished texture once; background identity/control and renderer differential routes pass.

### Reopened (2026-08-22)
REOPENED 2026-08-22 because its resolution was the wrong renderer contract. Whole-scene composition hid fractional per-layer sampling only by discarding full-output per-draw rendering, directly regressing issue #41. The cited render/background routes ran at 794x550 (scale 1), so they could not discriminate per-draw full-resolution rendering from a composition-sized target followed by an upscale. The native path is restored to full-output per-draw rendering; reverify scrolling at a fractional scale and fix any remaining phase error in per-draw source/placement sampling, not by flattening the scene.
