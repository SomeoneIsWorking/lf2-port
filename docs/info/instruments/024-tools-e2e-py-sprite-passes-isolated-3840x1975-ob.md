---
id: I024
kind: instrument
status: trusted
created: 2026-08-26
---

## Instrument

tools/e2e.py sprite_passes -- isolated 3840x1975 object-sprite sampling arms, edge locality, and inner/outer contour direction

## Validated by

Its synthetic 7x7 control must report one forbidden change in a flat field and zero forbidden changes beside an authored edge. A separate mask control must distinguish disjoint inner/outer sets and then detect one injected overlap. The live route requires isolated `aa`, `inner`, `aa,inner`, coarsening, and exterior-outline arms to produce their own answers while `nearest:2` stays byte-exact. On 2026-08-26 it reported 3,920 AA-changed fighter pixels, 3,320 strictly darker inner-contour pixels, 3,166 additional strictly darker pixels when composed with AA, and zero changes in flat 5x5 interiors. Inner and exterior arms retained 3,307 and 7,079 exclusive pixels respectively; their 13-pixel (0.39%) final-RGB overlap is fractional grown-quad rasterisation in the exterior arm, below the one-percent polarity gate.

## Known failure modes

(none recorded yet)
