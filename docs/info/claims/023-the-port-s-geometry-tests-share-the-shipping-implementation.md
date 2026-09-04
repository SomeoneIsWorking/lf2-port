---
id: C023
kind: claim
status: holds
created: 2026-08-11
tags: testing,geometry
depends: runtime/overrides/geom.h, runtime/overrides/background.c, runtime/video/ddraw.c
---

## Claim

The port's geometry tests share the shipping implementation rather than copying it: the overrides include runtime/overrides/geom.h, so ctest geometry exercises the code that ships

## Evidence

geom.h's eight functions are each called from the shipping path -- compose width and NATIVE_W/H from runtime/video/ddraw.c, object scale from runtime/video/render.c, parallax + camera bound + wide-view centring from runtime/overrides/background.c (which also aliases BG_SCREEN_W to GEOM_SCREEN_W via world.h), the overlay rows from runtime/overrides/screens.c, and pan from runtime/overrides/audio_pan.c. The retained native comparisons covered both 794x550 identity and widened composition; the current geometry unit test still exercises the shipping header rather than a copy.

## What would falsify it

a grep showing any geom.h function with no caller outside tests/test_geom.c -- that one is a copy again, and its test proves only that the copy works
