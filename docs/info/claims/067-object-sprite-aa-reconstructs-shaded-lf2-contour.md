---
id: C067
kind: claim
status: holds
created: 2026-08-26
tags: renderer,sprite-aa
depends: runtime/shaders/quad.frag#sample_chain
---

## Claim

Object-sprite aa reconstructs shaded LF2 contours directly at 3840x1975 output resolution without changing flat sprite interiors.

## Evidence

tools/e2e.py sprite_passes, current Clang build: every capture and engine target was 3840x1975; isolated aa changed 3,920 real-fighter pixels and 0 pixels in flat 5x5 output regions; nearest:2 stayed byte-exact; the synthetic locality selftest proved the checker can return the forbidden answer. tools/e2e.py fullres and render independently passed.

## What would falsify it

Falsified if the isolated aa arm changes fewer than 100 pixels, changes any pixel in a flat 5x5 region, reports or captures a non-3840x1975 target, or a visually inspected fighter retains staircased contours.
