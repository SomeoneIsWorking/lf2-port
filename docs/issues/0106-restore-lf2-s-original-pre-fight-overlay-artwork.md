---
id: 106
title: Restore LF2's original pre-fight overlay artwork
status: resolved
symptom: The pre-fight Fight/Reset/Stage/Difficulty panel is a port-authored blue outline UI rather than LF2's original bitmap-authored panel; restore the original presentation.
tags: reported,ui,rendering,overlay,original
created: 2026-08-24
updated: 2026-08-25
---

## Root cause

Commit `0caecaa` appended a port-authored outline-font panel over LF2's retained CHARMENU bitmap.
The screenshot therefore showed anti-aliased host labels and a new rounded panel rather than the
game's authored pixel text and layout.

## What was tried / dead ends

The original was never missing and did not need to be reconstructed; it remained underneath as a
fallback. Keeping both renderers would leave two authorities for the same panel.

## Resolution

The overlay-panel producer, CJK subset font, tests, and exact-output route were removed together
with their draw hooks. LF2's original CHARMENU blits and dynamic values now render directly. A
1920x1080 forced-overlay capture shows the original pixel-authored Fight/Reset/Background/
Difficulty/Exit panel with no host panel layered over it.

### Resolution (2026-08-25)
Removed the port-authored overlay panel, CJK subset, append hooks, and duplicate tests. A 1920x1080 forced-overlay capture shows LF2 original CHARMENU pixel artwork as the sole panel.
