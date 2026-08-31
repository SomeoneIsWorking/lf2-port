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
