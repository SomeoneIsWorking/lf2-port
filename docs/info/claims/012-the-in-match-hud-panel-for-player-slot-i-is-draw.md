---
id: C012
kind: claim
status: holds
created: 2026-08-05
tags: coop,hud,drop-in
depends: runtime/overrides/hud.c#fn_0041ae60, runtime/overrides/coop.c#coop_hud_preview
---

## Claim

The in-match HUD panel for player slot i is drawn from the SAME byte the stage pass and the world step read -- this+4+i, i.e. EXISTS at 0x00458b04+i -- so a slot can have a panel without a fighter only by raising that byte for the duration of the HUD pass alone

## Evidence

fn_0041ae60 loops i in 0..7 and branches on CMP byte [EDI+ESI+4],0 (human, object at this+404+4i) then CMP byte [ESI+EDI+0xe],0 (computer, object at this+404+4(i+10)), else leaves the box empty; ECX=EBX=this=0x00458b00 at both call sites 0x0041d765 and 0x00421a28. fn_0041a5a0 collects the stage's draw list at 0x0041a5d0 as 'for EAX in 0..0x18f: if ([EBX+EAX+4]) keep', and fn_004064d0 steps the world off the same byte. MEASURED, not only read: LF2_BLT_FRAME over a drop-in selection shows exactly 7 blits appearing and disappearing in the HUD band with the flash -- an 8x16 glyph at (401,0), the 40x45 portrait at (405,7) via obj->0x368->0x728, and four bar rows at (453,16)/(453,36) via fn_0043f310 -- while the joined mask at 0x00451288 and the device selector at 0x00450b4c were CONSTANT across both frames, which is what rules them out as the driver. With hud.c raising the byte for that pass only, scratch/hud19/frame_002306.png shows the panel lit with two fighters on the stage and frame_002310.png the panel dark with the same two.

## What would falsify it

a frame in which the joiner's panel is drawn while the gate byte reads 0 for the whole frame, or one in which the joiner appears on the stage while it reads 0 -- either would mean the panel and the stage are not both keyed on this+4+i
