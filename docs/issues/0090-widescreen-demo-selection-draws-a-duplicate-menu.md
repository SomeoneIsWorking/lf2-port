---
id: 90
title: Widescreen Demo selection draws a duplicate menu label
status: resolved
symptom: Selecting Demo on the widescreen mode menu leaves an unselected Demo label at the original position and draws the highlighted Demo label again to the right
tags: reported,widescreen,rendering,menu,demo
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

The stage-intro banner recognizer in `ddraw.c` used only a 794x600 source-sheet size and a
destination-y band of 294..341. The selected Demo row uses that same sheet at y=339. At wide
width it was therefore shifted once as a supposed stage banner and again by the ordinary fixed
screen framing, leaving the unselected row at its real position and the highlight 92 pixels to
the right. Both native width and any unselected row avoided the extra offset.

## What was tried / dead ends

The result-panel shifter was instrumented and never armed on the failing frame. GPU and software
captures both duplicated the row, proving the fault was shared composition policy rather than a
renderer-specific retained-list error.

## Resolution

### Resolution (2026-08-22)
Made match/HUD state part of stage-banner identity and extracted the rule to stage_banner.c; the selected Demo row is no longer double-shifted at 1920x1080.
