---
id: 94
title: Opening or closing the RmlUi pause modal transiently glitches the frame
status: open
symptom: Opening and closing RmlUi causes visible glitches during the transition; the corruption does not persist into gameplay.
tags: reported,ui,rmlui,pause,rendering
created: 2026-08-22
updated: 2026-08-22
---

## Reported

The user reports that opening and closing RmlUi causes visible glitches. The glitches are transient: they do not persist in the game after the modal transition.

## Constraint

Fix the frame-lifetime or composition transition that produces the transient corruption. Do not hide it with an extra redraw, delay, suppression, or screen-specific special case. The retained game frame, native display list, and RmlUi composition must have one explicit ownership/lifetime rule across both opening and closing.

## Investigation

The exact corrupted pixels and the first invalid frame are not yet measured. Inspect the pause open/close branches, the retained display-list rewind, and renderer/software parity; reproduce with deterministic frame dumps on the transition frames before changing behavior.
