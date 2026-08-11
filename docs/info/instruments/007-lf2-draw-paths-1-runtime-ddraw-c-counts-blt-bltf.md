---
id: I007
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

LF2_DRAW_PATHS=1 (runtime/video/ddraw.c) -- counts Blt, BltFast, and Lock/Unlock pairs that actually CHANGED pixels, reported together every 900 frames

## Validated by

Positive class present in every report: Blt=19753 over a run reaching a match, so the counters demonstrably run and the zeros beside them are not a dead instrument. The Lock arm is validated by construction rather than by count -- it hashes the surface at Lock and again at Unlock, so it distinguishes a lock taken to READ (which is not a draw) from one taken to write, which a bare lock count cannot. LIMIT, stated in the instrument's own output: BltFast's counter has never been observed non-zero on any run, so its 0 is consistent with both 'the game never calls it' and 'the counter is wrong'; it is one line adjacent to the one that works, which is as much as is available without a synthetic call. BLIND SPOTS PRINTED EVERY TIME, not only on an empty result: GDI text writes straight into the surface with no Lock (runtime/win32/gdi.c), and a lock whose writes cancelled out would hash the same -- both would read as no-draw. An all-zero run says so explicitly rather than reading as 'the game uses no route'.

## Known failure modes

(none recorded yet)
