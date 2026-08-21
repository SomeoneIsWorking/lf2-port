---
id: 82
title: RmlUi bypasses the existing high-resolution font and UI rendering path
status: resolved
symptom: RmlUi fonts and other UI elements no longer have the high-resolution rendering the port already had
tags: reported,ui,rmlui,rendering,hidpi,text
created: 2026-08-21
updated: 2026-08-21
---

## Reported

USER 2026-08-21: "What happened to the hires rendering we had, the fonts and such".

## Constraint

Determine which resolution owns RmlUi layout, font-atlas rasterization, render targets, and final composition. Preserve LF2's existing final-output-resolution text/UI path; do not merely enlarge a fixed atlas or add a DPI magic constant. Issue #80 only covered SVG device icons and does not prove the rest of RmlUi is high resolution.

### Resolution (2026-08-21)
The native-resolution game text path never disappeared; RmlUi had introduced a separate outline-font atlas and incorrectly sized dp layout from SDL_GetWindowPixelDensity instead of the display content scale. RmlUi now keeps drawable-sized geometry, uses SDL_GetWindowDisplayScale like Dusklight so FreeType rasterizes at the actual UI pixel size, and linearly samples generated coverage textures. The simulated 4K/200% run reports a 1588x1100 drawable, content scale 2.00, and a 32px raster for the 16dp body font.
