---
id: 106
title: Restore LF2's original pre-fight overlay artwork
status: closed
symptom: The pre-fight Fight/Reset/Stage/Difficulty panel is a port-authored blue outline UI rather than LF2's original bitmap-authored panel; restore the original presentation.
tags: reported,ui,rendering,overlay,original
created: 2026-08-24
updated: 2026-09-09
---

## Root cause
Commit `0caecaa` appended a port-authored outline-font panel over LF2's retained CHARMENU bitmap.
The screenshot therefore showed anti-aliased host labels and a new rounded panel rather than the
game's authored pixel text and layout.

## What was tried / dead ends
The original was never missing and did not need to be reconstructed; it remained underneath as a
fallback. Keeping both renderers would leave two authorities for the same panel.
