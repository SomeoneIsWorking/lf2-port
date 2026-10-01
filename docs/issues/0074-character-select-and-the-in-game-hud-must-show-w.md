---
id: 74
title: Character select and the in-game HUD must show which device controls which character
status: closed
symptom: The character-select panels and the in-match HUD do not say which device (keyboard / pad 1 / pad 2 ...) is driving which fighter; with drop-in coop and device mapping, a player cannot tell which slot is theirs
tags: reported,feature,hud,input
created: 2026-08-16
updated: 2026-09-09
---

## The constraint
- Do not fake it with a new overlay if the game's own panel can carry the label -- a label on
  the existing panel is the game's look; a second overlay is the port's.
- Devices are numbered as the input gather numbers them: 0 keyboard, 1..4 pads.

### Reopened (2026-08-21)
USER 2026-08-21: the keyboard SVG overlaps the Stage selector in the pre-fight settings overlay. The indicator needs a layout-owned anchor outside interactive labels/values at every scale.
