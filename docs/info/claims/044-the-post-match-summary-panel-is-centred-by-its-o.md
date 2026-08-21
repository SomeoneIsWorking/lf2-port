---
id: C044
kind: claim
status: holds
created: 2026-08-21
tags: widescreen,results
depends: runtime/video/result_panel.c#result_panel_blit_offset
---

## Claim

The post-match Summary panel is centred by its own 490-pixel bounds in a wide live composition without moving the HUD, stage, or mode caption.

## Evidence

2026-08-21: deterministic 1571x550 Stage/Lion_Forest frame 1688 measured the panel border at x=540..1030 (midpoint 785.0 versus view midpoint 785.5); tests/test_result_panel.c exercises the unrelated-art, cropped-source, native-width, caption, and frame-expiry negatives.

## What would falsify it

A deterministic wide post-match frame places the panel midpoint elsewhere, or any negative in test_result_panel moves a non-panel draw.
