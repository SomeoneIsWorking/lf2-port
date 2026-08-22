---
id: 99
title: Rear cast shadow darkens a foreground solid object
status: resolved
symptom: A boomerang behind the fighter projects its shadow over a carried foreground box instead of being occluded by final scene depth
tags: reported,rendering,shadows,hd2d,objects,depth
created: 2026-08-22
updated: 2026-08-22
---

## Reported

USER 2026-08-22: "there is a boomerang here behind the character, its shadow is rendering over the box"

## Root cause and implementation

The colour pass already stores the game's final painter-order D32 depth, but the cast-shadow
mask ignored it and flattened every projected silhouette into one full-screen texture. A rear
caster therefore darkened a later foreground solid as readily as the earlier-painted floor.

Every projected shadow vertex now carries its caster's same painter ordinal. The shadow mask
reloads completed scene depth, tests strict `LESS`, and never writes depth. Earlier floor depth
is farther and accepts the fragment; equal caster depth rejects self-shadow; a later foreground
object is nearer and rejects a rear shadow.

## Verified so far

The shipping GPU falsifier uses an opaque/transparent procedural foreground. Normal painter
order produces black/white/white at blocked, transparent and unobstructed samples; reversed
order produces white/white/white. The transparent half proves this is final scene visibility,
not suppression of the whole foreground rectangle.

### Reopened (2026-08-22)
Later foreground occlusion is GPU-verified, but the strict-depth mechanism now has two additional acceptance samples awaiting a serialized rerun: shadow received by earlier opaque ground and shadow rejected over the equal-depth caster. A named LEQUAL mutation must produce the opposite self-shadow answer.

The extended `tools/e2e.py visibility` arm paints an opaque ground receiver before the caster,
then the half-transparent foreground after it. It samples later opaque rejection, transparent
reception, earlier opaque reception, and equal-depth caster rejection independently. The named
`shadow-self-lequal` arm changes only the shadow compare to `LESS_OR_EQUAL`. The serialized
GPU rerun produced black/white/white/black for later opaque, transparent, earlier opaque and
equal-caster samples; reversed painter order produced white/white/white/black. The LEQUAL
mutation changed the equal-caster sample to white while leaving the other three answers
black/white/white. That is the required other answer for strict self-shadow rejection.

### Resolution (2026-08-22)
The cast-shadow mask now loads completed painter depth and tests projected fragments at caster ordinal with strict LESS/no depth writes. The GPU route proved later opaque rejection, transparent and earlier opaque reception, equal-depth self rejection (B/W/W/B), reversed order (W/W/W/B), and the LEQUAL mutation flipped only self to white (B/W/W/W).
