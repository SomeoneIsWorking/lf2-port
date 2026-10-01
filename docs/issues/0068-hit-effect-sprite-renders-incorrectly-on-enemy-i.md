---
id: 68
title: Native object pass draws hit effects from the glyph sheet
status: closed
tags: reported,renderer,sprite,combat
created: 2026-08-13
updated: 2026-09-09
---

## Root cause
Commit `9c07a7a` replaced the game's object pass, `fn_0041a5a0`, with
`runtime/overrides/objects.c`. Its effects loop passed `LD32(0x0044faf4)`, the glyph sheet, as
the `this` receiver to `fn_0043f010`. The original instructions at `0041ad17`, `0041ad6b`, and
`0041adbc` load the receiver from `0x0044f8fc`, the blood/impact-effect sheet. Consequently the
effect state and clip number were correct, but the clip was read from unrelated artwork.

### Reopened (2026-08-13)
User confirms identical hit bug with LF2_ENGINE=1 and LF2_RENDERER=soft. This falsifies renderer-specific texel sampling as the cause; defect exists in or before the software composition. Must compare against vanilla and trace the composing blit.
