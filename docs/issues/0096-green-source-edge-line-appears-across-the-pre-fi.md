---
id: 96
title: Green source-edge line appears across the pre-fight overlay
status: resolved
symptom: A bright green horizontal and vertical line appears along the top and left edge of the pre-fight overlay content
tags: reported,rendering,overlay,sampling,texture
created: 2026-08-22
updated: 2026-08-22
---

## Reported

USER 2026-08-22: "green line"

The supplied screenshot shows a one-pixel bright green L-shaped line: horizontally above the Stage row and vertically down the left edge. Identify the source rectangle, texture sampling, colour-key, or destination-boundary cause. Fix the shared rendering contract; do not hide the line with an inset, crop, overdraw, per-screen suppression, or asset-specific colour replacement. Verify at the magnified shipping renderer scale and include a negative that exposes adjacent/source-edge texel bleed.

### Note (2026-08-22)
Reproduced at the reported 3840x1975 full-output scale. The Stage highlight is blit #11 from the unkeyed 794x600 overlay sheet, source (330,254)-(609,276) to logical destination (15,87)-(294,109). The native capture contained exactly 1,080 #00ff1e pixels: a 1,002px top run and 79px left run; the 1070x550 software control contained zero. A source-lifetime trace found identical edge pixels at blit recording and deferred texture upload, ruling out mutation. The first clamp hypothesis was falsified: the overlay is drawn by the legacy render list, not the engine shader, and clamp/no-clamp procedural engine arms produced the same pixels.

The render list transformed the 279x22 logical destination into its roughly 1002x79 output rectangle, then treated those raster dimensions as proof of a DirectDraw stretch. That moved the first covered fragment to adjacent source (329,253), the green separators, rather than requested (330,254). The repaired shared contract keeps both units: DirectDraw's logical source/destination rectangles classify each axis as 1:1 or stretched. A logical 1:1 axis retains its texel-centre interval under magnification; a true stretched axis maps once across the output fragments it actually covers. Classic and engine call the same helper with both rectangles.
Using logical destination dimensions unconditionally was also rejected: it fixes magnified 1:1 copies but gives a true 41->17 stretch the wrong sampling rate once magnified. The first route was rejected too because it did not pin or prove the classic path consumed its negative injector. The final offline gate covers integer and fractional output placement through the helper used by both renderers; the runtime route pins classic and parses its zero-engine-frame report before accepting either image.

### Resolution (2026-08-22)
The shared classic/engine helper now preserves both coordinate units per axis: DirectDraw's logical rectangles distinguish 1:1 copies (texel centres remain invariant under magnification) from true StretchBlts (mapped once over the actually covered output fragments, including fractional phase). Offline 41->17 gates pass 51 integer-position and 52 fractional-position fragments; blanket-logical negatives differ on 41 and 43. The pinned classic 3840x1975 route parsed zero engine frames and measured 0 green pixels versus the injected old raster-as-stretch path's exact 1,080-pixel L at x548..1549/y312..390.
