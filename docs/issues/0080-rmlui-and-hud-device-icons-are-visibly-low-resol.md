---
id: 80
title: RmlUi and HUD device icons are visibly low-resolution
status: resolved
symptom: Shared keyboard/gamepad SVG indicators look low-resolution in the high-resolution renderer and RmlUi.
tags: reported
created: 2026-08-20
updated: 2026-08-21
---

USER 2026-08-20: 'These icons are very low res and bad' and 'We already had hi-res rendering'. Cause: the new asset bridge bypassed that existing high-resolution path. `device_icon_record` rasterized an SVG to the 18x18 logical HUD size and uploaded those pixels to the display-list quad, so the native renderer could only enlarge a tiny bitmap. RmlUi similarly inherited an undersized nearest-filtered texture. Preserve the logical HUD size and existing native renderer, but supply it a renderer-resolution vector raster; rasterize the RmlUi copy at UI resolution with linear minification. Do not enlarge the HUD cell or add LF2-local art.

### Resolution (2026-08-21)
The SVG bridge bypassed LF2's existing high-resolution native renderer by rasterizing icons to the 18x18 logical HUD size before display-list scaling; RmlUi also used an undersized nearest-filtered texture. SVGs now rasterize directly at each consumer's requested resolution: the existing native quad gets the icon's current output-pixel footprint behind its unchanged 18x18 logical cell and refreshes it when scale changes, RmlUi gets 120x120 with linear minification, and software gets logical size. settings, a 1920x1080 HUD capture, and the simulated 4K/200% hidpi route verify the shared embedded assets through real rendering.
