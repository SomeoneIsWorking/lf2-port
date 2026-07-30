---
id: 3
title: Moon scene = built-in background 99 from PE resources; moon drawn without color key
status: open
symptom: match on a flat green/blue stage with street lamps and a moon in an opaque black box; looks like a broken stage
tags: rendering,background,bg99,colorkey,resources
created: 2026-07-30
updated: 2026-07-30
---

The scene is NOT a broken bg/sys stage: it is the game's built-in background (id 99) assembled from PE resource bitmaps -- BACK99_1 (rail bar), BACK99_2 (the moon), BACK99_3 (the lamp), flag from BARS-adjacent art -- over flat programmatic fills. The flat bands are how it is designed to look. Verified by dumping all 64 RT_BITMAP resources (scratch/frames/rsrc/).

**The one real defect:** the moon (BACK99_2) draws as an opaque black box -- its black background should be transparent. h_StretchBlt ignores its rop argument (runtime/gdi.c), so if the game uses a SRCAND/SRCPAINT pair for this draw the port copies opaquely; alternatively the surface misses its SetColorKey. Needs a live bg-99 repro to trace which; background selection is random, so build a deterministic repro first (find the bg-index variable, or add a diagnostic override).

Selecting Background at the pre-fight overlay by script is timing-fragile (docs/running.md warns); a first .data diff across a cycle press did not isolate the index.
