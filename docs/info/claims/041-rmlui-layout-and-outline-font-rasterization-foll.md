---
id: C041
kind: claim
status: holds
created: 2026-08-21
tags: rmlui,hidpi,text
depends: runtime/ui/settings_ui.cpp#rmlui_render, runtime/ui/rmlui_backend.cpp#GenerateTexture
reconfirmed: 2026-08-21
verified_at: 2026-08-21 10:55:20
---

## Claim

RmlUi layout and outline-font rasterization follow the drawable and display content scale: on the simulated 2x output its 794x550-point window has a 1588x1100 context and the 16dp body font computes to 32px.

## Evidence

tools/e2e.py hidpi passed 2026-08-21 on nested KWin at output scale 2.00; it read the shipping document's rmlui metrics line and refused unscaled output.

## What would falsify it

a scaled-display run reports context dimensions different from the drawable or a 16px body font at content scale 2

## Re-confirmed 2026-08-21

Verified at commit e005304 by tools/e2e.py hidpi: a 794x550 logical window produced a 1588x1100 drawable at content scale 2, full-drawable game composition, and a 32px RmlUi body font from the 16dp rule.
