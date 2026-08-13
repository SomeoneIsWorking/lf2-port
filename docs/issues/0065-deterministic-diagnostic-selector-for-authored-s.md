---
id: 65
title: Deterministic diagnostic selector for authored Stage Mode backgrounds
status: resolved
symptom: A gallery or per-stage rendering test cannot select a named PvE background safely: VS uses a random/default selection and Stage Mode routes do not expose each background.
tags: reported,stage,renderer,diagnostic,testing
created: 2026-08-13
updated: 2026-08-13
---

The selector must be diagnostic-only and must validate a stage name against the game-loaded background registry. It must write the game’s own background-index word only while a pre-fight selection is live, then release before the match; no hardcoded background index, new menu, or persistent feature switch. The negative must name an unknown stage and show it changed nothing. This is required to capture and verify every authored PvE scene.

### Resolution (2026-08-13)
Added LF2_STAGE_PREVIEW=<underscore-name>. It resolves against the game-loaded background registry, temporarily swaps the guest index only during the background/geometry draw, then restores it before game state can read it. Soft-renderer positive: Lion_Forest selected with 732 substitutions, 732 restorations, 0 failed; unknown-name arm: 0 substitutions and the original Brokeback Clif stayed selected. ctest 13/13 passed.
