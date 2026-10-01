---
id: 94
title: Opening or closing the RmlUi pause modal transiently glitches the frame
status: closed
symptom: Opening and closing RmlUi causes visible glitches during the transition; the corruption does not persist into gameplay.
tags: reported,ui,rmlui,pause,rendering
created: 2026-08-22
updated: 2026-09-09
---

## Constraint
Fix the frame-lifetime or composition transition that produces the transient corruption. Do not hide it with an extra redraw, delay, suppression, or screen-specific special case. The retained game frame, native display list, and RmlUi composition must have one explicit ownership/lifetime rule across both opening and closing.

### Note (2026-08-22)
Follow-up audit removed the last spent/fl_touch deferred-clear state: present now clears list lengths, overlay boundaries, tile-arena use, and pool claims immediately. ui_global no longer accepts coverage as proof; it runs a deterministic no-match-modal control at the same @match offsets, requires exact outside-document pixels while open, and byte-identical full frames for the first hidden frame (+362) and two successors.
