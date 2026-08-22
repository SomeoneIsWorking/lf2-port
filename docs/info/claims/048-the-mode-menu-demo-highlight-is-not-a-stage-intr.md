---
id: C048
kind: claim
status: holds
created: 2026-08-22
tags: widescreen,rendering
depends: runtime/video/stage_banner.c#stage_banner_offset, runtime/video/ddraw.c#surf_Blt
---

## Claim

The mode-menu Demo highlight is not a stage-intro banner even though it uses the same 794x600 sheet at y=339; a running-match signal is required for banner identity.

## Evidence

At 1920x1080 both GPU and software duplicated Demo before; LF2_BLT_FRAME showed its 794x600/y339 draw. The exact GPU @modemenu+20 capture after stage_banner_offset requires HUD/match state has one selected row.

## What would falsify it

if a widescreen Demo capture duplicates again, or an actual stage-intro banner is no longer centred
