---
id: I020
kind: instrument
status: trusted
created: 2026-08-22
---

## Instrument

tools/e2e.py overlay_sampling — exact-green pre-fight source-edge capture at 3840x1975

## Validated by

The detector self-test distinguishes a synthetic six-pixel L from an all-black control. The corrected route pins `LF2_ENGINE=0`, enables the renderer report, and refuses either arm unless it parses `engine: not drawing (...). 0 frame(s), 0 quad(s) in 0 batch(es)`. On 2026-08-22 both arms proved that classic path: the shared 1:1 mapping produced 0 #00ff1e pixels, while `LF2_TEXRECT_RASTER_DEST` reproduced 1,080 at x=548..1549/y=312..390, a 1,002px horizontal run and 79px vertical run.

## Known failure modes

The first version did not pin or assert the classic renderer, even though its negative injector
was classic-only. A persisted/default engine selection could therefore make the negative a
no-op. The route must retain both its `LF2_ENGINE=0` pin and parsed zero-engine-frame report;
either one alone is not sufficient evidence.
