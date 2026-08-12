---
id: I015
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

tests/test_stagelight.c -- the stage's one light, walked offline

## Validated by

Two mutants, each caught by 2 failing checks: flipping the sign of stagelight_shadow's across term fails both azimuth-direction assertions, and removing the elevation clamp fails both above-the-floor assertions. 46 checks, no SDL and no GPU, in ctest. It exists because the light previously lived inside hd2d.c, where the only instrument was a screenshot -- and a set lit fifteen degrees wrong looks like a set.

## Known failure modes

(none recorded yet)
