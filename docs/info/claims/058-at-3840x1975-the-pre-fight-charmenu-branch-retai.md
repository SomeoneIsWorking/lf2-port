---
id: C058
kind: claim
status: holds
created: 2026-08-22
tags: rendering,ui,issue-84
depends: runtime/ui/overlay_panel.c, runtime/ui/overlay_panel.h, runtime/ui/ui_rgba.c, runtime/overrides/text.c, runtime/overrides/geom.h, runtime/video/ddraw.c, runtime/video/render.c, tools/routes/overlay_labels_test.py, assets/fonts/DroidSansFallback-LF2OverlaySubset.ttf
---

## Claim

At 3840x1975 the pre-fight CHARMENU branch retains every authored draw, then appends a 1092x596 native outline panel whose 20 CJK glyphs and all 12 Latin segments contain output-resolution detail; forced native-panel failure exposes the complete authored labels underneath.

## Evidence

2026-08-22 tools/e2e.py overlay_labels on rebuilt integrated tree, exact PIDs 4036773/4037888/4039513: VS 194/194 and Stage 614/614 appended/final, all CJK cells >=493 coverage and >=392 within-logical-cell edges, all Latin segments >=193 coverage and >=203 edges; logical-nearest and native-CJK/nearest-Latin negatives retained coverage but scored zero; forced fallback 0 appended/194 failures retained original row coverage [4827,9805,11635,4432]. Captures: scratch/overlay_labels_test/{vs,stage,fallback}/frame_*.ppm.

## What would falsify it

if overlay_labels reports a moved CJK rectangle, any Latin/CJK run lacks native within-logical-cell detail, any renderer resource drop occurs, or the forced-failure capture loses the original static labels
