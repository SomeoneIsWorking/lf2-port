---
id: 82
title: RmlUi bypasses the existing high-resolution font and UI rendering path
status: closed
symptom: RmlUi fonts and other UI elements no longer have the high-resolution rendering the port already had
tags: reported,ui,rmlui,rendering,hidpi,text
created: 2026-08-21
updated: 2026-09-09
---

## Constraint
Determine which resolution owns RmlUi layout, font-atlas rasterization, render targets, and final composition. Preserve LF2's existing final-output-resolution text/UI path; do not merely enlarge a fixed atlas or add a DPI magic constant. Issue #80 only covered SVG device icons and does not prove the rest of RmlUi is high resolution.
