---
id: 84
title: Image-authored menu labels remain low-resolution at native output scale
status: open
symptom: The pre-fight overlay still shows pixelated Fight, Reset, Stage, Difficulty, Exit and Chinese labels even when outline-font text is rendered at output resolution
tags: reported,rendering,text,ui,assets
created: 2026-08-21
updated: 2026-08-21
---

## Reported

USER 2026-08-21: "fonts are still low res". The supplied pre-fight-overlay screenshot makes the large English/Chinese labels the dominant visible example.

## Root cause

Those labels are not emitted through `TextOutA` or the game font sheets. They are baked into the shipped screen bitmap, so the native Liberation glyph path and its output-resolution raster cannot affect them. The dynamic Stage/Difficulty values are separate host glyphs.

## Constraint

Do not call this fixed by changing font sampling: replacing image-authored labels requires a native overlay layout and a redistributable CJK-capable outline face, while preserving the game’s input-row geometry. Do not commit extracted game art.

## Resolution
