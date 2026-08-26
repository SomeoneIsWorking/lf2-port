---
id: C068
kind: claim
status: holds
created: 2026-08-26
tags: renderer,shader,sprite
depends: runtime/shaders/quad.frag#inner_contour_coverage, runtime/video/spritefilter.h#SpriteChain, runtime/video/engine.c#sprite_uniform, tools/routes/sprite_passes_test.py#main
---

## Claim

The player-visible inner object-sprite contour is a one-output-fragment RGB-only treatment on the authored-covered side of the silhouette and composes independently with aa

## Evidence

2026-08-26: tools/e2e.py sprite_passes at 3840x1975 measured inner vs base as 3,320 strictly darker pixels and 0 other/flat-interior changes; aa,inner vs aa as 3,166 strictly darker pixels and 0 other/flat-interior changes; inner and exterior-outline arms retained 3,307 and 7,079 exclusive pixels. ctest spritefilter passed the parser, own-draw, and zero-geometry-margin assertions.

## What would falsify it

Any inner arm brightens a channel, changes flat 5x5 interiors, requires a positive geometry margin, changes alpha, or no longer produces substantial pixels exclusive of outline:1 at 3840x1975.
