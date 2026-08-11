---
id: C023
kind: claim
status: holds
created: 2026-08-11
tags: testing,geometry
depends: runtime/overrides/geom.h, runtime/overrides/background.c, runtime/ddraw.c
---

## Claim

The port's geometry is shared with its offline test rather than copied into it: the overrides include runtime/overrides/geom.h, so ctest geometry exercises the code that ships

## Evidence

geom.h's eight functions are each called from the shipping path -- compose width and NATIVE_W/H from runtime/ddraw.c, object scale from runtime/render.c, parallax + camera bound + wide-view centring from runtime/overrides/background.c (which also aliases BG_SCREEN_W to GEOM_SCREEN_W via world.h), the overlay rows from runtime/overrides/screens.c, the pan from runtime/overrides/audio_pan.c. Verified after the factoring, on real data: tools/e2e.sh background PASSED with both byte-identity arms at 794x550 and both LF2_BG_SKEW control arms still differing, so the parallax and camera went through geom.h without moving a pixel; tools/e2e.sh widescreen PASSED all five composition cases; tools/e2e.sh mouse PASSED reaching charselect, overlay and match, which is the overlay row table. ctest is 8 tests in 1.22 s, and tools/build_matrix.sh PASSES gcc/clang at two optimisation levels each.

## What would falsify it

a grep showing any geom.h function with no caller outside runtime/test_geom.c -- that one is a copy again, and its test proves only that the copy works
