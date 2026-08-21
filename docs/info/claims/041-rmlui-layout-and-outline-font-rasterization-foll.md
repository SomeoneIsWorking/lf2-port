---
id: C041
kind: claim
status: holds
created: 2026-08-21
tags: rmlui,hidpi,text
depends: runtime/ui/settings_ui.cpp#rmlui_render, runtime/ui/rmlui_backend.cpp#GenerateTexture
---

## Claim

RmlUi layout and outline-font rasterization follow the drawable and display content scale: on the simulated 2x output its 794x550-point window has a 1588x1100 context and the 16dp body font computes to 32px.

## Evidence

tools/e2e.py hidpi passed 2026-08-21 on nested KWin at output scale 2.00; it read the shipping document's rmlui metrics line and refused unscaled output.

## What would falsify it

a scaled-display run reports context dimensions different from the drawable or a 16px body font at content scale 2
