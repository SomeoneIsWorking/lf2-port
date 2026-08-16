---
id: 73
title: Stage mode: the full-screen swipe and the STAGE n-n announcement are anchored to 794
status: open
symptom: At a wide window the stage-intro full-screen swipe transition and the 'STAGE n-n' announcement banner are laid out for the 794-wide screen and do not follow the wide view
tags: reported,widescreen,stage-mode
created: 2026-08-16
updated: 2026-08-16
---

## Reported

The user asked for the stage-mode fullscreen swipe and the stage number announcement to be
adjusted to widescreen. This is DISTINCT from issue #54 (the persistent Man/HP/STAGE-n-n UI
row, already resolved via a GDI text band).

## The constraint

- Issue #54 settled the persistent status row. The 'STAGE n-n' ANNOUNCEMENT here is the
  intro banner that swipes at the start of a stage, a different draw with (probably) a
  different path -- do not assume it is the same GDI text.
- The full-screen swipe is likely a game-owned full-screen fill/blit transition; it needs to
  span the wide composition like the other full-screen fills do (docs/running.md, issue #42),
  i.e. the flat colour is extended while centred art is not stretched.

## To characterize

- Find which function draws the stage-intro swipe and the announcement (likely in the object
  pass fn_0041a5a0 or the stage-draw path fn_0043f010), via LF2_DRAW_PATHS / LF2_TEXT_DEBUG /
  LF2_BLT_RECTS, before touching a constant. The fix must use screen_offset_x() / the same
  framing the other screen furniture takes, not a new literal.
