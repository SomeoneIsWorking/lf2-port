---
id: 125
title: Android touch controls disappear when a controller is reported
status: resolved
symptom: The user cannot see the round Attack, Defend, and Jump controls even though touch controls are enabled.
tags: reported,android,touch,ui
created: 2026-08-31
updated: 2026-08-31
---

REPORTED 2026-08-31. The circular sword/shield/double-chevron controls are present in runtime/ui/touch_controls.c, but runtime/input/touch_input.cpp disables every touch visual whenever SDL reports any controller. On Android the explicit Touch controls setting must own overlay visibility; controller presence alone cannot erase the player's controls. Settings remains the way to hide them.

### Resolution (2026-08-31)
The round sword/shield/double-chevron controls were still rendered by runtime/ui/touch_controls.c, but Android disabled the entire overlay when any controller was discovered. Android now respects the explicit Touch controls setting; Settings remains the player-controlled way to hide the overlay.

### Reopened (2026-08-31)
REPORTED 2026-08-31: The player sees the old controls alongside the round set. Visibility must follow the device actually used, not controller connection; controller input hides the touch set and a touch begins showing it again. Keep exactly one round touch-control renderer.

### Resolution (2026-08-31)
There is one renderer: runtime/ui/touch_controls.c. PresentationState now records actual input, not hardware presence: a gamepad button or meaningful stick motion hides that renderer, and a finger-down restores it. The production state seam is covered by test_touch_layout; Android's explicit Touch controls setting still disables the overlay entirely.
