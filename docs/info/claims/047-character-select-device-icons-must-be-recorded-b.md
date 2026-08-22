---
id: C047
kind: claim
status: holds
created: 2026-08-22
tags: rendering,device-icons
depends: runtime/video/ddraw.c#surf_Blt, runtime/ui/device_icons.h#device_icon_charselect_phase
---

## Claim

Character-select device icons must be recorded before the pre-fight panel blit; the overlay keeps SCREEN_CHARSELECT, so a present-time label is above it.

## Evidence

1920x1080 GPU route: before frame_000957 had the pad icon over Background; after moving the record point, the same @overlay+50 frame has no icon above the panel while @charselect+50 still shows it.

## What would falsify it

if a real overlay capture shows a device icon above the panel, or character selection no longer shows its icon
