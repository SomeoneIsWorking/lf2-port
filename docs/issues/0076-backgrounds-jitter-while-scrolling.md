---
id: 76
title: Backgrounds jitter while scrolling
status: resolved
symptom: Stage background layers visibly jitter as the camera scrolls instead of moving stably with their authored parallax. The cause must be located in camera/parallax coordinates or renderer sampling; a magic offset, per-stage special case, or hiding the motion is not an acceptable fix.
tags: reported,renderer,background,scrolling,parallax
created: 2026-08-20
updated: 2026-08-20
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-20)
The jitter came from independently placing every scrolling layer at fractional output coordinates, which changed each nearest-sampling phase as the camera moved. The engine now composes the entire world on the native integer grid and scales the finished texture once; background identity/control and renderer differential routes pass.
