---
id: C038
kind: claim
status: holds
created: 2026-08-20
tags: renderer,lighting
depends: runtime/shaders/hd2d_light.frag, runtime/video/engine.c, tools/routes/render_test.sh
---

## Claim

The HD2D light changes character/shadow pixels only; a frame with no fighters is unchanged.

## Evidence

tools/e2e.sh render light arm: the character-select/menu frame changes exactly 0 pixels, while the matched fighter frame changes 5666 pixels by up to 152 levels; the negative skip arms prove the comparison can fail.

## What would falsify it

a no-fighter menu/background frame differs with lighting enabled, or a fighter frame no longer differs
