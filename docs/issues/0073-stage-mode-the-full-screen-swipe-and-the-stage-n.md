---
id: 73
title: Stage mode: the full-screen swipe and the STAGE n-n announcement are anchored to 794
status: closed
symptom: At a wide window the stage-intro full-screen swipe transition and the 'STAGE n-n' announcement banner are laid out for the 794-wide screen and do not follow the wide view
tags: reported,widescreen,stage-mode
created: 2026-08-16
updated: 2026-09-09
---

## The constraint
- Issue #54 settled the persistent status row. The 'STAGE n-n' ANNOUNCEMENT here is the
  intro banner that swipes at the start of a stage, a different draw with (probably) a
  different path -- do not assume it is the same GDI text.
- The full-screen swipe is likely a game-owned full-screen fill/blit transition; it needs to
  span the wide composition like the other full-screen fills do (docs/running.md, issue #42),
  i.e. the flat colour is extended while centred art is not stretched.
