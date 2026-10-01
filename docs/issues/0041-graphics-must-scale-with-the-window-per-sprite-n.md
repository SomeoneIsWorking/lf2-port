---
id: 41
title: Graphics must scale with the window, per sprite -- not by upscaling the composed screen
status: closed
symptom: in a window taller than 550 the game's picture sits in a 550-row band with black above and below; it should fill the window, and it should do so by drawing every sprite larger at draw time rather than by scaling a finished 794x550 frame
tags: reported,rendering,renderer,widescreen,scaling
created: 2026-08-11
updated: 2026-09-09
---

### Reopened (2026-08-22)
REPORTED AGAIN 2026-08-22. The native engine regressed to composing the world into a 1069x550 intermediate and scaling that completed image to a 3840x1975 drawable. The 1069x550 dimensions are field-of-view coordinates, not raster dimensions: every sprite/mesh must be transformed into and sampled in the full drawable, and host fonts/SVGs must be rasterized there. At 3840x1975 geom_compose_rect gives 3838.682px of coverage starting at x=0.659, which rasterises as a 3839px rectangle at x=1 and visibly leaves a band. Do not hide the band with a clear or an edge extension; restore the per-draw full-resolution render contract described by this issue and derive exact output coverage from the viewport transform.
