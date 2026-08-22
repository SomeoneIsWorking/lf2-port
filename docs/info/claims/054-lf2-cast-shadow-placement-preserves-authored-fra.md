---
id: C054
kind: claim
status: holds
created: 2026-08-22
tags:
depends: runtime/video/stagelight.h#stagelight_shadow_quad, runtime/video/shadowcaster.h#shadowcaster_should_cast, runtime/overrides/objects.c#fn_0041a5a0, runtime/video/engine.c#lighting_passes, runtime/video/engine_visibility_probe.c#engine_visibility_probe_run
reconfirmed: 2026-08-22
verified_at: 2026-08-22 16:26:34
---

## Claim

LF2 cast-shadow placement preserves authored frame contact, includes held physical casters
independently of character shading, and obeys earlier/equal/later painter depth

## Evidence

The production-header relations reject the former recentering. Real shipping masks at
`frame_001369` measured two distinct grounded LF2 fighters at 0 px and 1 px foot-contact gaps;
two airborne controls remained separated by 9 px, and the analyser rejects issue #72's old
7 px/12 px recentering. The shipping GPU probe verified carried W/W/B vs fighter-only W/B/B.
Its strict-depth arm verified later/transparent/earlier/equal as B/W/W/B, reversed painter
order as W/W/W/B, and the LEQUAL other-answer mutation as B/W/W/W.

## What would falsify it

an authored frame's projected foot span differs from its fn_0040de30 destination placement, a held physical object's exclusive shadow sample is absent, or a rear caster darkens an opaque later painter

## Re-confirmed 2026-08-22

Post-commit tools/e2e.py visibility shadow_contact passed: carried-object and strict-depth mutation arms passed, and two grounded LF2 fighters measured 0px/1px contact gaps.
