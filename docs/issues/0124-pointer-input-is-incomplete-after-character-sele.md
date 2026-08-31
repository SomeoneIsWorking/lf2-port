---
id: 124
title: Pointer input is incomplete after character selection
status: investigating
symptom: Touch must work through every menu, and mouse input does not operate the difficulty-selection UI reached after character selection.
tags: reported,input,mouse,touch,menu,difficulty,android
created: 2026-08-31
updated: 2026-08-31
---

REPORTED 2026-08-31. Issue #6 claimed every screen was pointer-operable, but its mapped post-load screens ended at the pre-fight overlay. Identify the difficulty picker in the binary and its real selection/confirmation state, then route both mouse and Android touch through one shared pointer-to-menu action path. Do not special-case a scripted route or translate arbitrary taps directly in the Activity.

### Note (2026-08-31)
CAUSE 2026-08-31: touch_input consumed all finger events for the authored action overlay while win32 intentionally discarded SDL's synthetic touch-mouse events, leaving non-control taps with no route into LF2's pointer hit tests. Unclaimed contacts now use the same hostwin window-to-guest pointer path as physical mouse; action-zone contacts remain multi-touch controls. The real mouse route now proves the pre-fight difficulty state changes 0 -> 2 -> 1 and reaches a match. Android touch acceptance remains pending on hardware.
