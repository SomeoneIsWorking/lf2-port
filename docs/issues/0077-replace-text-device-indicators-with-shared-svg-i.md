---
id: 77
title: Replace text device indicators with shared SVG icons
status: resolved
symptom: Character-select and HUD device indicators still render K/P text instead of graphical keyboard/gamepad icons, and reusable port UI art needs a shared source of truth.
tags: reported,ui,input,assets,svg
created: 2026-08-20
updated: 2026-08-20
---

## Constraint

The keyboard and controller indicators must use reusable SVG artwork owned by the shared
port-assets project, not local copies or runtime machine paths. LF2 must embed/rasterize those
assets for both its software compositor and native GPU display list. The same shared loader must
serve RmlUi so the SVG lookup and decoding rules are not duplicated.

The project and global porting guide must record port-assets as the default home for reusable
cross-port artwork.

### Resolution (2026-08-20)
Keyboard/gamepad SVGs now live in shared port-assets with a manifest, generator and raster/visual checks. LF2 embeds them and one device-assets module rasterizes them for RmlUi, software pixels and GPU tiles; character-select/HUD no longer draw K/P text. The settings route loads both shared textures and render screenshots show the in-game icon.
