---
id: 65
title: Deterministic diagnostic selector for authored stage previews
status: closed
symptom: authored PvE scenes had no reproducible way to select a stage for capture
tags: diagnostics,stage,preview
created: 2026-08-12
updated: 2026-09-09
---

Constraint worth keeping: the selector is diagnostic-only, resolves a stage name
against the game-loaded background registry, writes the game's own background-index
word only while a pre-fight selection is live, and restores it before the match. No
hardcoded index, no new menu, no persistent feature switch.

Delivered as `LF2_STAGE_PREVIEW=<underscore-name>`. The full record is in git history.
