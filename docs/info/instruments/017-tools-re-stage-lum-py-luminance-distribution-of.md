---
id: I017
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

tools/re/stage_lum.py -- luminance distribution of every shipped background layer, offline

## Validated by

--selftest feeds BOTH classes through the same path: a key-black/pure-white BMP in BI_RGB and in BI_RLE8 (key excluded, 8 lit px, all 8 above 0.75) and a mid-grey one (8 lit px, ZERO selected at 0.50, all 8 present below it) -- so an all-black decode cannot pass by reporting an empty lit set. A missing corpus is asserted to exit 2 saying it searched nothing rather than printing a clean zero, and any layer that fails to decode is named and makes the run exit non-zero. Cross-checked against an independent derivation: it reports 0.437% of bc >=0.50 and the GPU-side readback of a live bc frame reports 0.501%.

## Known failure modes

(none recorded yet)
