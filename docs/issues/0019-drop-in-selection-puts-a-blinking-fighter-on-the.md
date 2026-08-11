---
id: 19
title: Drop-in selection puts a blinking fighter on the stage; only the joiner's HUD should be live until they lock in
status: resolved
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
rectangles, the source surface and the caller, and HUD_BAND_H=118 in runtime/video/ddraw.c already
brackets the band. Trace a frame with the joiner gated and one with it not, and the panel's
driver is the difference.

DO NOT park the fighter off-camera to fake this. It would look fixed and would not be.

### Resolution (2026-08-05)
The joiner is now OUT of the world for the whole of its selection and chooses in its HUD panel instead.

WHAT WAS NOT KNOWN, and is the reason this was blocked: what the game draws a player's HUD panel from. It is fn_0041ae60, and it reads THE SAME BYTE as the stage -- this+4+i, the port's EXISTS at 0x00458b04+i. Its loop over the eight slots is 'if (this[4+i]) draw from this+404+4i; else if (this[0xe+i]) draw a computer's from this+404+4(i+10); else leave the box empty', while fn_0041a5a0 builds the stage's draw list at 0x0041a5d0 as 'every k in 0..399 whose this[4+k] is set' and fn_004064d0 steps the world off the same byte. So the guess recorded in this entry -- 'the panel looks to be driven by the object being in the world' -- was right, and the alternative (the joined mask at 0x00451288 or the selector at 0x00450b4c) is ruled out by measurement, not by reading: LF2_BLT_FRAME over a selection shows the seven panel blits appearing and disappearing with the flash while both of those were CONSTANT.

THE FIX. With one byte serving three passes, the two have to disagree, and the only place they can is between them. fn_0041ae60 is now overridden (runtime/overrides/hud.c) to raise the byte for a slot that is still choosing, call the game's own panel drawing, and put it back down. The stage pass and the world step run outside that window and never see the slot: the joiner cannot walk, be hit, or be seen. The panel that appears is the GAME'S -- its portrait, its bars, its name plate, read off the record coop.c built -- so nothing here paints a picture of a character-select screen. The flash moved with it: it is now the panel that lights and darkens on the eight-frame period, and coop_select_tick no longer touches the world at all. The fighter enters the world at exactly one point, the lock-in.

VERIFIED on real frames, not by argument: scratch/hud19/frame_002306.png has the third panel lit with the candidate's portrait and full bars and TWO fighters on the stage; frame_002310.png, eight frames later, has that panel empty and the same two. tools/e2e.sh coop_select asserts it every run -- the selection now prints the gate byte as read from outside the HUD pass on every frame it reports, and the test fails if any of them is 1 OR if none was reported at all. That grep was run against a doctored log carrying a '= 1' line to confirm it can fail, and against an empty one to confirm it refuses rather than passing on no evidence.

WHAT WAS DELIBERATELY NOT DONE, as this entry warned: the fighter is not parked off-camera, and its pixels are not suppressed at the blit. Either would leave it walking about, colliding and taking damage, and would look fixed without being.
