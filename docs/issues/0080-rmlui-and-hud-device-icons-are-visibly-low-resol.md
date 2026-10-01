---
id: 80
title: RmlUi and HUD device icons are visibly low resolution
status: closed
symptom: the shared SVG device icons looked bad, after the port already had a high-resolution path
tags: reported,ui,svg,hidpi
created: 2026-08-20
updated: 2026-09-09
---

Root cause: the new asset bridge bypassed the existing high-resolution path.
`device_icon_record` rasterized an SVG to the 18x18 logical HUD size and uploaded those
pixels to the display-list quad, so the native renderer could only enlarge a tiny bitmap;
RmlUi inherited an undersized nearest-filtered texture.

Dead end: enlarging the HUD cell or adding LF2-local art. Keep the logical 18x18 cell and
the existing native renderer, and supply each consumer a vector raster at the resolution
it actually draws at. The full record is in git history.
