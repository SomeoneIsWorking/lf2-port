---
id: 114
title: Add a narrow inner contour to smoothed object sprites
status: closed
symptom: The edge-only AA reconstructs the outside silhouette but leaves the sprite's chunky authored boundary texels visually exposed, so the result remains more pixelated than desired.
tags: reported,renderer,shader,options
created: 2026-08-26
updated: 2026-09-09
---

USER 2026-08-26: "Hmhmh yeah, looks right but it doesn't give the effect I hoped so I think we can add a little inner contour just to hide the pixelation on the sprite itself around the edges"
The existing `aa` pass is behaving as specified: it changes classified edge wedges and leaves flat interiors untouched. The remaining visual problem is different: LF2's opaque boundary texels still form a high-contrast staircase immediately inside the reconstructed coverage. Add a narrow contour on the sprite side of the silhouette, through the visible ordered sprite-pass settings path. It must be bounded to the inner edge band, preserve transparency and flat interiors, and have a running 3840x1975 falsifier that distinguishes it from the existing outer `outline` pass.
Do not blur or darken the whole sprite, grow the exterior silhouette, hide the feature behind an `LF2_*` variable, or duplicate edge classification outside the authoritative shader/filter owner.
