---
id: 74
title: Character select and the in-game HUD must show which device controls which character
status: open
symptom: The character-select panels and the in-match HUD do not say which device (keyboard / pad 1 / pad 2 ...) is driving which fighter; with drop-in coop and device mapping, a player cannot tell which slot is theirs
tags: reported,feature,hud,input
created: 2026-08-16
updated: 2026-08-16
---

## Reported

The user asked that character select and the in-game HUD show which device is controlling which
character. This is the natural companion to the device-mapping UI (#70) and drop-in coop.

## What exists

- runtime/overrides/input.c tracks which device drives which player slot (dev_player[0..4]:
  keyboard then pads, mapped to player slots). That is the ground truth for 'which device'.
- runtime/overrides/hud.c draws the in-match panel strip (fn_0041ae60), and coop.c builds the
  drop-in panel. The game's own panel is a portrait + bars read off the record.
- Character selection is the game's own screen (screens.c drives its mouse/pad).

## The constraint

- Do not fake it with a new overlay if the game's own panel can carry the label -- a label on
  the existing panel is the game's look; a second overlay is the port's.
- Devices are numbered as the input gather numbers them: 0 keyboard, 1..4 pads.
