---
id: 83
title: Enemy sprite sometimes disappears while held object remains visible
status: resolved
symptom: During gameplay an archer can become completely invisible while the rock it is holding still renders at the correct position
tags: reported,rendering,sprite,hd2d,enemy
created: 2026-08-21
updated: 2026-08-21
---

## Reported

USER 2026-08-21: "enemies sometimes go invisible, there is an archer here holding this rock".
The supplied screenshot shows the rock at the expected held position while the archer sprite is
absent. Preserve that distinction while tracing: the object exists and its attached object's draw
survives, so this is not evidence that the gameplay object was deleted.

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-21)
The renderer's lifetime-only 512-texture pool filled during Stage mode and returned NULL forever for every later sheet, so a cached held rock survived while the later archer sheet vanished. engine_textures.c now owns a frame-safe LRU cache: current-frame entries cannot be evicted, replacement is committed only after allocation/upload succeeds, and dirty state is explicit. The texture_cache route reached Stage, filled all 512 entries, performed 168 evictions with 123 peak live/frame, and reported zero failed requests and zero dropped art.
