---
id: I016
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

LF2_ENGINE_GBUF=1 -- read the engine's G-buffer back and report the distances actually in it

## Validated by

Caught a real fault on its FIRST use: it latched on frame 1, the front-end menu, and reported an entirely zero buffer -- true and useless. Now retries every 60th frame until one carries a distance, bounded at 40, and reports the bound rather than going quiet. Checked against an independent source: on Brokeback Clif it reports 1.0000 for the span-1500 layers and 1.2061 for the span-1379 ones, which is (1500-794)/(span-794) from the stage's own bg.dat (C031) at half-float precision. Its normal channel has both classes: 90 px with a real surface normal when a .stage is authored, 0 px with none.

## Known failure modes

(none recorded yet)
