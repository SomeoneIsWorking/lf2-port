---
id: 34
title: The key light is too angled; it should come from the top, slightly left
status: resolved
symptom: reported. The rig is (-0.55, 0.74, 0.38) in stage axes, which is a low side-light. Wanted: mostly overhead with a slight lean to the left
tags: lighting
created: 2026-08-06
updated: 2026-08-06
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-06)
The key is overhead now, leaning slightly left: (-0.25, 0.94, 0.22), i.e. azimuth -49,
elevation 70. It was (-0.55, 0.74, 0.38), a low side light. Key and ambient were rebalanced
(1.20 -> 1.48, 0.62 -> 0.66) so a flat camera-facing pixel still lands within a couple of per
cent of the colour the game drew -- the light stays a difference from flat rather than a
brightness. It is no longer a constant either: issue #37 put it on the pause menu.
