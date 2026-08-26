---
id: I024
kind: instrument
status: trusted
created: 2026-08-26
---

## Instrument

tools/e2e.py sprite_passes -- isolated 3840x1975 object-sprite sampling arms and flat-interior locality check

## Validated by

Its synthetic 7x7 control must report one forbidden change in a flat field and zero forbidden changes beside an authored edge; the live route then required isolated aa to change at least 100 pixels. On 2026-08-26 it reported 3,920 AA-changed fighter pixels and zero in flat 5x5 interiors, while nearest:2 remained byte-exact.

## Known failure modes

(none recorded yet)
