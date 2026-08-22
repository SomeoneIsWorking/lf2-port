---
id: I021
kind: instrument
status: trusted
created: 2026-08-22
---

## Instrument

tools/routes/backdrop_pixels.py — Lion Forest no-hole/no-seam, distinct-camera and native-rectangle acceptance gate

## Validated by

Positive synthetic fixture plus nine negatives: black/key hole, three hard joins, forbidden same-frame mutation, static-band mutation, equal camera, scaled main layer, and scaled continuation. Real 1302x550 captures at guest cameras 1385/1898 produced 0 holes, 0/0/0 join excess, 0 static changed bytes, 12,493 moving-band changed bytes, and 10+1324 equal source/destination rectangles.

## Known failure modes

The pixel coordinates and join positions are Lion Forest-specific; this instrument cannot
certify another stage. The scrolling arm requires the same two frame specifications in
`LF2_FRAME_DUMP` and `LF2_BLT_FRAME`. Background diagnostics call the blit trace's shared
frame matcher so the camera records and captures cannot silently resolve different frames.
