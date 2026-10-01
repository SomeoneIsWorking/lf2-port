---
id: 98
title: Lifted objects do not cast a projected shadow
status: closed
symptom: A carried rock is visibly raised above the stage but contributes no silhouette to the native cast-shadow mask
tags: reported,rendering,shadows,hd2d,objects
created: 2026-08-22
updated: 2026-09-09
---

## Root cause and implementation
`EngineQuad.is_object` conflated character shading with shadow casting, and render.c assigned
it only to the first texture after a ground-ellipse marker. A held physical item's flat ellipse
may be suppressed while carried, so it never became a caster. Marking it `is_object` would be
equally wrong because the lighting pass would relight the rock as character skin.
The hand-ported `fn_0041a5a0` now scopes each `fn_0040de30` world-object draw with its exact z
row and independent character/caster facts. Ordinary game ellipse eligibility remains
authoritative; data.txt's physical item types 1, 2, 4 and 6 continue casting while carried.
`EngineQuad.casts_shadow` is separate from `is_object`. The production policy has positive
checks for light/heavy/thrown/drink types and a negative effect type.
