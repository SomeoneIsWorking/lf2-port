---
id: 74
title: Character select and the in-game HUD must show which device controls which character
status: open
symptom: The character-select panels and the in-match HUD do not say which device (keyboard / pad 1 / pad 2 ...) is driving which fighter; with drop-in coop and device mapping, a player cannot tell which slot is theirs
tags: reported,feature,hud,input
created: 2026-08-16
updated: 2026-08-17
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

### Note (2026-08-17)
The in-match HUD half is DONE (2026-08-17): each of the eight HUD panels gets a device label -- K for the keyboard, P1..P4 for the pads -- drawn at the present (ddraw.c's hud_device_labels) at the panel's own geometry (fn_0041ae60: ((i&3)*0xc6, (i>>2)*0x36), plus hud_offset_x on a wide view), gated on the in-match HUD. device_for_player() (input.c) is the reverse of the existing dev_player table. Verified on a frame dump: 'P1' renders at panel 0's top-left; controller/controller_2p routes pass. STILL OPEN: the CHARACTER-SELECT half -- the slot boxes there are drawn by a different function, whose geometry is not yet located (fn_00431d10 draws the overlay's own items, not the slots).
