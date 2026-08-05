---
id: 19
title: Drop-in selection puts a blinking fighter on the stage; only the joiner's HUD should be live until they lock in
status: open
symptom: while a late joiner is choosing, their character is already standing in the match blinking, instead of the choice showing only in that player's HUD panel
tags: reported,coop,drop-in,character-select,hud,rendering
created: 2026-08-05
updated: 2026-08-05
---

REPORTED IN PLAY. The selection was built as a flashing fighter ON THE STAGE. What is wanted
is the fighter NOT in the match until lock-in, with only that player's HUD panel live and
showing the character being cycled.

WHY THE OBVIOUS FIX DOES NOT WORK, so nobody tries it and ships it: "just don't build the
fighter until lock-in" leaves NO HUD EITHER. The HUD panel appears and disappears in
lockstep with the object's gate byte -- visible in the frame dumps from the first
implementation (game/scratch/frame_002348.png with the gate down has no portrait and no bars
in that slot; frame_002403.png with it up has both). So the panel looks to be driven by the
object being in the world, and the choice cannot simply be moved out of it.

WHAT MUST BE ESTABLISHED FIRST, and is NOT known: what the game draws a player's HUD panel
from. Specifically whether the panel is emitted per LIVE OBJECT (the gate) or per JOINED
PLAYER (the mask at 0x00451288 / the device selector at 0x00450b4c), and where the portrait
comes from. The evidence above is one frame pair and is consistent with either, because the
run had the gate and the join move together.

THE INSTRUMENT EXISTS: LF2_BLT_FRAME=<frame> lists every blit composing a frame with both
rectangles, the source surface and the caller, and HUD_BAND_H=118 in runtime/ddraw.c already
brackets the band. Trace a frame with the joiner gated and one with it not, and the panel's
driver is the difference.

DO NOT park the fighter off-camera to fake this. It would look fixed and would not be.
