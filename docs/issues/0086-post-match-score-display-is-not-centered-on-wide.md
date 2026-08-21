---
id: 86
title: Post-match score display is not centered on widescreen
status: resolved
symptom: The Summary/results panel remains near the original 794-wide screen area instead of centering within the live ultrawide composition
tags: reported,widescreen,results,ui,layout
created: 2026-08-21
updated: 2026-08-21
---

## Reported

USER 2026-08-21: "the score display should be centered". The supplied ultrawide screenshot shows the Summary/results panel anchored left of the composition center.

## Root cause

The post-match branch is inline in the game's monolithic `fn_0041bc90`. It computes the
panel's Y coordinate from the live row count, but calls the screen draw helper with literal
X `0x96` (150). Normal in-match composition deliberately has no whole-screen centring offset,
because it is a wider world; consequently the 490-pixel Summary panel retained its original
794-wide X coordinate.

## Resolution

`runtime/video/result_panel.c` owns a frame-scoped recognizer for the complete 490x61 Summary
header and 490x32 footer seen in the decompiled branch. It applies the authoritative
fixed-item centring geometry only to panel blits and matching GDI text between those boundaries.
The HUD, stage, and bottom mode caption remain in their wide-world positions.

At a 1571x550 composition the real render moved the panel from x=150..640 to x=540..1030;
its midpoint is the composition midpoint. `tests/test_result_panel.c` includes unrelated
and cropped-source negatives, native-width identity, text, footer, caption, and frame-expiry
checks.
