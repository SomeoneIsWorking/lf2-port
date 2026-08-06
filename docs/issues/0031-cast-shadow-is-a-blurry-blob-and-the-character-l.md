---
id: 31
title: Cast shadow is a blurry blob and the character lighting is too faint to see
status: resolved
symptom: reported with a screenshot of an airborne fighter: the cast shadow on the ground below is a shapeless dark smear with no silhouette left in it, and the directional lighting on the fighter is too subtle to read as angled light. Wanted: a CRISP shadow that keeps the sprite's shape, and lighting that is visibly angled and visibly on the characters
tags: reported,rendering,lighting,shadows
created: 2026-08-06
updated: 2026-08-06
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-06)
The blur is gone and the light is stronger and clearly angled.

CRISP SHADOW. The mask was downsampled to half resolution and Gaussian-blurred on the way
into the lighting. That is what a soft shadow wants and it is not what this game wants: a
32-pixel sprite's silhouette, halved and then blurred, is a shapeless dark smear with none of
the fighter left in it -- which is exactly what the screenshot showed. The mask is now sampled
at full resolution, 1:1, and the blur shader and the two scratch targets that existed only to
feed it are deleted.

THE LIGHT IS VISIBLE. Two things were flattening it. The Sobel of the silhouette was being
divided by 4 to stop it tipping the normal into the screen plane -- correct -- but the bevel
strength was then left at 0.55 and the ambient at 0.90, so the key was a small term on top of
a large flat one. Bevel is 0.90, ambient 0.62 and key 1.20, chosen so a flat camera-facing
pixel still lands within a couple of percent of the colour the game drew: the light is a
DIFFERENCE from flat, not a brightness. The lit and shaded sides of a fighter now differ by
about 40% either way, and the warm key against the cool sky puts a temperature difference
between them as well.

Verified: ctest render, four arms, including the one that matters here -- the light changes
184174 px on the match frame and ZERO on the character-select frame.
