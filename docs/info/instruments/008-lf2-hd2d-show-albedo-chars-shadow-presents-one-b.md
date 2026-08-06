---
id: I008
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

LF2_HD2D_SHOW=albedo|chars|shadow -- presents one buffer of the lighting chain instead of the lit frame

## Validated by

CAUGHT LYING ONCE, and fixed. The first version looked its buffers up at the END of hd2d_post, by which time the half-resolution scratch that had held the cast-shadow mask had been reused by a later blur -- so LF2_HD2D_SHOW=shadow presented a blurred copy of the whole scene, which read as 'the shadow mask contains the entire picture' and sent the investigation after a bug that did not exist. It now shows each buffer at the point that buffer is final and returns immediately, so a later stage cannot overwrite what is being looked at. Validated against both classes after the fix: SHOW=shadow on a match frame gives a black field with white silhouettes reaching 255 (measured: 3099 px above 200, brightest 255), and those same pixels are exactly the ones darkened in the lit frame -- at (610,631) mask=255, lit=(18,24,49) against unlit (41,54,108), which is the 0.45 factor the shadow strength asks for. A name it does not recognise is reported at exit rather than silently showing the finished frame.

## Known failure modes

(none recorded yet)
