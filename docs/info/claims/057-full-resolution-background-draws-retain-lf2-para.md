---
id: C057
kind: claim
status: holds
created: 2026-08-22
tags:
depends: runtime/overrides/geom.h#geom_layer_offset_phase, runtime/video/raster.h#raster_place_x, runtime/video/render.c#entry_output_x, runtime/overrides/background.c#fn_0041a250, tools/routes/ppm.py#read_ppm, tools/routes/parallax_jitter.py#accept, tools/routes/parallax_jitter_test.py#authenticate_log, tools/routes/parallax_jitter_test.py#camera_sequence_error
reconfirmed: 2026-08-22
verified_at: 2026-08-22 17:52:07
---

## Claim

Full-resolution background draws retain LF2 parallax fractional remainders without altering the native 1x raster

## Evidence

geom_layer_offset_phase supplies the exact quotient remainder to the shared classic/engine entry_output_x transform. test_geom proves the rate and absolute position to 0.0001 output pixels, rejects integer stalls/jumps, and preserves 1x placement and a signed-int overflow boundary. `tools/e2e.py parallax_jitter` serially authenticated both real arms: 32/32 route actions fired, Lion Forest background 1 survived match initialization, the native engine targeted 3440x1440, and 21 traced frames moved the camera. Its analyzer measured 0 accepted stalls and 109..820 changed bytes versus 18 stalls and 0..4859 for LF2_BG_INTEGER_RASTER=1.

## What would falsify it

A magnified scrolling layer stalls or catches up by a whole logical-pixel block, classic and engine use different placement transforms, a native 1x comparison changes, or the integer-raster negative no longer produces stalls and a larger jump.

## Re-confirmed 2026-08-22

All 33 offline tests passed, including the 0.0001-pixel production-helper gate and analyzer
other-answer checks. The offscreen `tools/e2e.py parallax_jitter` route serially authenticated
32/32 fired actions, Lion Forest background 1 after match initialization, a 3440x1440 native
engine target, 21 moving-camera traces and 21 captures in each arm, then measured accepted
stalls=0 at 109..820 changed bytes versus integer-negative stalls=18 at 0..4859. The saved
camera sequences match exactly; the durable route now rejects a trajectory mismatch before
pixel analysis, with an offline deliberate mismatch proving that gate can fail.

## Re-confirmed 2026-08-22

Commit cb3951b built with Clang; all 34 offline tests passed. The final serial 3440x1440 parallax route authenticated matching moving-camera sequences and measured 0 accepted stalls at 109..820 changed bytes versus 18 integer-negative stalls and a 4,859-byte catch-up jump.
