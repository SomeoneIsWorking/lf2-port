---
id: C043
kind: claim
status: holds
created: 2026-08-21
tags: rendering,texture-cache
depends: runtime/video/engine_textures.c#engine_texture_for, runtime/video/texture_lru.h#texture_lru_choose
---

## Claim

The engine texture cache can exceed 512 distinct lifetime sheets in Stage mode without losing later sprite art because only entries unused by the current frame are evicted.

## Evidence

tools/e2e.py texture_cache reached a Stage match at 1920x1080, filled 512 resident entries, performed 168 evictions with 123 peak live/frame, and reported 0 failed requests and 0 dropped quads.

## What would falsify it

A real Stage run reports a failed texture request, dropped quad, or invisible sprite after cache churn; or current-frame texture protection is removed.
