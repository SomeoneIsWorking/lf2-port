---
id: I018
kind: instrument
status: trusted
created: 2026-08-21
---

## Instrument

LF2_BLT_FRAME (`runtime/video/blt_trace.c`) records destination object identity, dimensions and
primary status together with the complete destination/source rectangles, flags, caller, and
colour fill for selected presented frames.

## Validated by

2026-08-21: selected frame 1688 produced 180 entries including both the null-source COLORFILL clear and non-null keyed surface blits, then the expected primary copy and an explicit 180-blit total; selecting that frame exposed both answers rather than uniform output.

2026-08-22: the failing widescreen Demo frame reported its selected-row draw on a non-primary
978x550 destination from the 794x600 source while the same trace still named the final primary
copy. That differing destination status is the positive control for the added fields; it ruled
out primary presentation and located the shared composition policy.

## Known failure modes

(none recorded yet)
