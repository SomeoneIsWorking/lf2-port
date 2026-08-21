---
id: I018
kind: instrument
status: trusted
created: 2026-08-21
---

## Instrument

LF2_BLT_FRAME (runtime/video/blt_trace.c) records the complete destination/source rectangles, flags, caller, and colour fill for selected presented frames.

## Validated by

2026-08-21: selected frame 1688 produced 180 entries including both the null-source COLORFILL clear and non-null keyed surface blits, then the expected primary copy and an explicit 180-blit total; selecting that frame exposed both answers rather than uniform output.

## Known failure modes

(none recorded yet)
