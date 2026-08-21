---
id: 87
title: Stage background does not cover ultrawide composition
status: resolved
symptom: On an ultrawide Stage-mode results screen, background layers end early and expose black rectangular gaps inside the expanded view
tags: reported,widescreen,background,stage,rendering
created: 2026-08-21
updated: 2026-08-21
---

## Reported

USER 2026-08-21: "the background doesn’t adapt to ultrawide". The supplied screenshot shows black gaps where the mountain/forest background layers end inside the live composition.

## Constraint

Use each layer’s authored span/loop and the live composition width. Do not horizontally
stretch props, hide gaps with a flat fill, or add screenshot-specific offsets.

## Root cause

The semantic far backdrop was already widened across the live composition, but its bitmap
ends at y=198. At the original 794-pixel width, later colour-keyed forest layers cover that
lower edge. The ultrawide view exposes authored horizontal gaps between those later pieces
(one ends at x=1100 and the next starts at x=1216), leaving no surface behind them below
y=198. The black rectangles are therefore genuine uncovered backing, not failed colour-key
transparency; the traced surfaces had a valid key range of 0..0 and used `DDBLT_KEYSRC`.

## Resolution

`runtime/video/backdrop.h` plans a clamp-to-edge continuation of the semantically marked far
backdrop's final source row behind the later keyed layers. It repeats existing backdrop pixels
only; it does not stretch or tile a prop, add new geometry, or guess from the screenshot. The
extension stops at the measured world/UI boundary y=528 so the game's 22-row black mode-caption
strip remains intact. The software and native-engine display lists use the same rectangle.

On the reproduced 1571x550 Lion Forest result frame, the formerly black x=1100..1216,
y=198..219 gap contains zero black pixels after the fix. `tests/test_backdrop.c` verifies the
semantic gate, source-row clamp, live width, world boundary, empty source, and no-op cases.
