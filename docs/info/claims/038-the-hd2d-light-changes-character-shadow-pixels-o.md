---
id: C038
kind: claim
status: holds
created: 2026-08-20
tags: renderer,lighting
depends: runtime/shaders/hd2d_light.frag, runtime/video/engine.c, tools/routes/render_test.sh
reconfirmed: 2026-08-21
verified_at: 2026-08-21 11:37:26
---

## Claim

The HD2D light changes character/shadow pixels only; a frame with no fighters is unchanged.

## Evidence

tools/e2e.sh render light arm: the character-select/menu frame changes exactly 0 pixels, while the matched fighter frame changes 5666 pixels by up to 152 levels; the negative skip arms prove the comparison can fail.

## What would falsify it

a no-fighter menu/background frame differs with lighting enabled, or a fighter frame no longer differs

## Re-confirmed 2026-08-21

tools/e2e.py render passed after the engine texture-cache extraction and host-tile sampler change: the light changed zero pixels on the no-fighter frame and 5,666 pixels on the fighter frame, while the engine reproduced the software compositor within the established tolerance.
