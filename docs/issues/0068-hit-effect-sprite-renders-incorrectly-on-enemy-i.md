---
id: 68
title: Hit effect sprite renders incorrectly on enemy impact
status: resolved
tags: reported,renderer,sprite,combat
created: 2026-08-13
updated: 2026-08-13
---

USER (2026-08-13): "hitting an enemy, the hit effect is wrong too"

The impact effect must be reproduced in both software and GPU render paths. Determine whether the defect is source-rectangle/color-key sampling, blend/compositing, animation selection, or placement. Do not repaint or replace game assets. Acceptance: the original hit-effect sprite renders without edge leakage, corruption, incorrect blending, clipping, or placement during a real enemy hit.

## Root cause

The same missing guest-surface cache invalidation as issue #67 could retain a prior upload of a
short-lived effect surface.  A sampled hash is not a valid dirty mechanism for a small animated
region: the changed rows and columns can all fall between samples.

## Resolution

DirectDraw blits, fills and unlocks, plus GDI sprite loads, now explicitly invalidate both GPU
texture caches.  A deterministic VS route delivered repeated attacks and captured 60 frames at
six-frame intervals around real contacts.  With lighting disabled, the engine impact frames are
pixel-identical in sprite shape, placement and keying to the software-composed reference; with
lighting enabled they remain intact and differ only by the intended scene lighting.
