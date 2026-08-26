---
id: 114
title: Add a narrow inner contour to smoothed object sprites
status: resolved
symptom: The edge-only AA reconstructs the outside silhouette but leaves the sprite's chunky authored boundary texels visually exposed, so the result remains more pixelated than desired.
tags: reported,renderer,shader,options
created: 2026-08-26
updated: 2026-08-26
---

USER 2026-08-26: "Hmhmh yeah, looks right but it doesn't give the effect I hoped so I think we can add a little inner contour just to hide the pixelation on the sprite itself around the edges"

The existing `aa` pass is behaving as specified: it changes classified edge wedges and leaves flat interiors untouched. The remaining visual problem is different: LF2's opaque boundary texels still form a high-contrast staircase immediately inside the reconstructed coverage. Add a narrow contour on the sprite side of the silhouette, through the visible ordered sprite-pass settings path. It must be bounded to the inner edge band, preserve transparency and flat interiors, and have a running 3840x1975 falsifier that distinguishes it from the existing outer `outline` pass.

Do not blur or darken the whole sprite, grow the exterior silhouette, hide the feature behind an `LF2_*` variable, or duplicate edge classification outside the authoritative shader/filter owner.

### Resolution (2026-08-26)
The remaining staircase was the authored opaque boundary texel, not a failure of the edge-wedge AA. Added an independent player-visible `inner` terminal pass and GRAPHICS toggle. `quad.frag` derives a one-output-fragment band from authored alpha occupancy, paints black only on the covered side, preserves sampled alpha, and needs zero quad margin; `aa` remains unchanged and the existing exterior outline still owns growth. The 3840x1975 `sprite_passes` route measured 3,320 strictly darker inner-only changes and 3,166 additional changes under `aa,inner`, with zero brightening or flat-interior changes and thousands of pixels exclusive from the outer-outline arm. The parser now also refuses malformed outline widths instead of accepting trailing junk or an empty/zero token.
