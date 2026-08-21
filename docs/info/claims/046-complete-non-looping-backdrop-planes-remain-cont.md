---
id: C046
kind: claim
status: holds
created: 2026-08-21
tags: widescreen,background,renderer
depends: runtime/video/backdrop.h#backdrop_plane_span, runtime/overrides/background.c#fn_0041a250, runtime/video/ddraw.c#surf_Blt, runtime/video/texrect.h#texrect_for_blit
---

## Claim

Complete non-looping backdrop planes remain continuous when widened beyond their authored span

## Evidence

2026-08-21: Lion Forest bg.dat has two-piece planes at spans 1100 and 1400. Production-policy tests map the 1100 join to one shared x=1142 boundary in a 1571 view and exclude looping layers, ground, CUHK lamps, and grass. The exact 1571x550 engine frame has no interior bitmap cutoff and matches the software compositor within max channel difference 2; the 794 route remains byte-identical to the recompiled body.

## What would falsify it

A selected plane piece separates from its neighbour or ends inside the viewport, an isolated prop or looping layer is scaled, the 794 output changes, or the wide engine/software backdrop pixels diverge beyond tolerance.
