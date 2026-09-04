---
id: C038
kind: claim
status: holds
created: 2026-08-20
tags: renderer,lighting
depends: runtime/shaders/hd2d_light.frag, runtime/video/engine.c, the recorded render runtime scenario
reconfirmed: 2026-08-21
verified_at: 2026-08-21 11:37:26
---

## Claim

The HD2D light changes character/shadow pixels only; a frame with no fighters is unchanged.

## Evidence

The recorded renderer comparison's light arm changed exactly 0 pixels on the character-select/menu frame and 5,666 pixels by up to 152 levels on the fighter frame; its negative arms proved the comparison could fail.

## What would falsify it

a no-fighter menu/background frame differs with lighting enabled, or a fighter frame no longer differs

## Re-confirmed 2026-08-21

The recorded renderer comparison passed after the engine texture-cache extraction and host-tile sampler change: the light changed zero pixels on the no-fighter frame and 5,666 pixels on the fighter frame, while the engine reproduced the software compositor within the established tolerance.
