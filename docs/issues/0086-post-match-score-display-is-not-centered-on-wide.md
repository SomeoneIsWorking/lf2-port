---
id: 86
title: Post-match score display is not centered on widescreen
status: closed
symptom: The Summary/results panel remains near the original 794-wide screen area instead of centering within the live ultrawide composition
tags: reported,widescreen,results,ui,layout
created: 2026-08-21
updated: 2026-09-09
---

## Root cause
The post-match branch is inline in the game's monolithic `fn_0041bc90`. It computes the
panel's Y coordinate from the live row count, but calls the screen draw helper with literal
X `0x96` (150). Normal in-match composition deliberately has no whole-screen centring offset,
because it is a wider world; consequently the 490-pixel Summary panel retained its original
794-wide X coordinate.
