---
id: 84
title: Image-authored menu labels remain low-resolution at native output scale
status: resolved
symptom: The pre-fight overlay still shows pixelated Fight, Reset, Stage, Difficulty, Exit and Chinese labels even when outline-font text is rendered at output resolution
tags: reported,rendering,text,ui,assets
created: 2026-08-21
updated: 2026-08-22
---

## Reported

USER 2026-08-21: "fonts are still low res". The supplied pre-fight-overlay screenshot makes the large English/Chinese labels the dominant visible example.

## Root cause

Those labels are not emitted through `TextOutA` or the game font sheets. They are baked into the shipped screen bitmap, so the native Liberation glyph path and its output-resolution raster cannot affect them. The dynamic Stage/Difficulty values are separate host glyphs.

## Constraint

Do not call this fixed by changing font sampling: replacing image-authored labels requires a native overlay layout and a redistributable CJK-capable outline face, while preserving the game’s input-row geometry. Do not commit extracted game art.

## Draw ownership and implementation

The decompiled owner is `FUN_00429730`, not GDI. Its CHARMENU branch at
`0x0042cba0..0x0042cd2b` draws clip 8 as the complete static panel at `(3,3)`, clip 15 as
its footer at `(3,159)`, clips 9..14 as the six selected-row variants, and clips 23/22 as
the Stage-mode row variant. Only after those does it call `FUN_00401290` for the dynamic
background/stage value at `(174,91)` and difficulty at `(174,115)`.

`runtime/ui/overlay_panel.c` therefore appends the complete native panel after that branch's
exact final static producer: the selected clip 9..14 in non-Stage modes, clip 23 in Stage,
or clip 22 when Stage row 3 is selected. It does not suppress any CHARMENU blit. The complete
original authored panel remains underneath, so a later tile allocation, texture, or upload
failure reveals the game's bitmap rather than a missing UI. The native draw still precedes
the branch's dynamic `TextOutA` values, which retain ordinary painter order.

The native layout shares `GEOM_OV_ROW_Y` with the shipping hit test and takes each staggered
selected-row x from the six literal producer calls. Its software raster goes into the usual
composition while a separate host tile is rasterised at the exact output pixel dimensions,
so live resize cannot reuse a lower-resolution panel. `MENU_CURSOR == 1` selects the Stage
label because FUN_00429730 receives that word as `param_4`; the exploratory build's rejected
`GAME_MODE_WORD == 1` test visibly swapped the VS and Stage labels.

The opaque native base also hand-authors the two black dynamic-value wells the original bitmap
provided. Both begin at the decompiled `TextOutA` x=174 anchor, share the panel's inner right
margin, and derive their vertical bounds from the same row geometry as input. Without them the
sharp Random/Difficult/Stage value glyphs remained functional but lost their contrast on blue;
the route now samples both wells in both captured modes.

The English runs use the existing embedded Liberation Sans. The Traditional Chinese runs
use `assets/fonts/DroidSansFallback-LF2OverlaySubset.ttf`, a fontTools subset of Android's
Apache-2.0 Droid Sans Fallback source. It contains exactly the 17 required codepoints; the
source, modification and full licence are documented in `assets/fonts/README.md`.

## Resolution

Verified by the rebuilt `tools/e2e.py overlay_labels` acceptance. Its explicit VS and Stage arms
ran serially at 3840×1975 and checked the engine's exact target, the native 1092×596 panel raster,
194/194 VS and 614/614 Stage panel/final-producer counts with originals retained, dynamic
Background/Stage and Difficulty values, both value-well pixels, `dropped=0`, zero texture requests
failed, and exact-size captures. The route pins the six reported CJK rectangles, then requires
coverage and within-logical-cell edge detail in every one of their 20 glyph cells and in every
Latin-before/after segment, including Background/Stage and Difficulty. A nearest-upscaled negative
preserves glyph coverage but has zero per-glyph edge scores; a second hybrid pastes native CJK over
that nearest panel and still has zero Latin edge scores. Both must produce the other answer. A third
forced-native-failure arm appends 0/194 panels and still finds the complete original static labels,
authenticating the no-suppression fallback rather than trusting a diagnostic claim. Native-resolution
inspection of the complete frames and unscaled panel crops confirmed one clean rounded edge, aligned
Latin/CJK baselines, punctuation, black-well contrast and spacing, with pixel-scaled game art unchanged
around the output-resolution panel.

### Resolution (2026-08-22)
Replaced bitmap-authored CHARMENU labels with a complete native panel appended after the exact final producer, retaining every original draw as fallback. Exact 3840x1975 VS/Stage/fallback acceptance authenticates the 1092x596 raster, every Latin/CJK run against nearest and hybrid negatives, dynamic values, wells, zero resource drops, and visible authored labels under forced native failure.
