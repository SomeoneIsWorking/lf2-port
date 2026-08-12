---
id: C035
kind: claim
status: holds
created: 2026-08-12
tags: rendering,hd2d,bloom
depends: tools/re/stage_lum.py
---

## Claim

No luminance threshold, absolute or relative, can drive a bloom over LF2's shipped stage art: it spans 0.000% to 22.602% of pixels above 0.75 luminance across stages, and the brightest is over 1% pure white

## Evidence

tools/re/stage_lum.py over all 133 shipped background layers (black colour key excluded; BI_RLE8 decoded in-tool because Pillow fails on 43 of them, and a scan that skipped those would report the brightness of the subset it decoded). bc: 0.000% >=0.75, max luminance 0.541 -- nothing at or above 0.55 selects a single pixel. gw: 22.602% >=0.75, top-1% percentile 1.000 -- a percentile threshold still selects its whole sky, which is genuinely near-white art (no shipped layer keys on white). Confirmed frame-side by LF2_ENGINE_GBUF=1 on bc: 766 px above 0.75 luminance, ZERO of them carrying a G-buffer distance; the world's own luminance is 0.501% >=0.50 with nothing above it.

## What would falsify it

a stage ships whose art is authored with HDR or an emissive channel, or the shipped game/bg set changes
