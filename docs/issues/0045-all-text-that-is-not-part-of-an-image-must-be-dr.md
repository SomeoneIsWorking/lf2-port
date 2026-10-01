---
id: 45
title: All text that is not part of an image must be drawn from a vector font shipped in the repo
status: closed
symptom: text is rasterised at the composition's resolution and then scaled with everything else, so it is the one thing in the frame that does not get sharper as the window grows
tags: reported,rendering,text,gdi,scaling,feature
created: 2026-08-11
updated: 2026-09-09
---

### Reopened (2026-08-21)
USER 2026-08-21: 'fonts are still low res'. The supplied screenshots are the game's pre-fight overlay and in-match HUD, not RmlUi. The dynamic values and HUD text use the fixed-cell font-sheet path in runtime/win32/gdi.c; the overlay's large labels are image-authored and are tracked separately in #84. Verification must exercise the shipping renderer rather than only report a requested raster scale.
