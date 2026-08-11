---
id: C025
kind: claim
status: holds
created: 2026-08-12
tags: widescreen,hud,stage-mode
depends: runtime/overrides/background.c#fn_0041a5a0
---

## Claim

The player's number tag is pinned by the GAME's own 794-wide screen clamp inside fn_0041a5a0, not by anything the port does or fails to do

## Evidence

TWO MEASUREMENTS, and the second one discriminates where every earlier attempt on issue #55 did not.

THE CODE, from re/instructions.tsv, 0041a9c9..0041aa33 -- the tag's x is world - camera, clamped low at 0 and high at 794 less the string's pixel width:

    0041a9e2  SUB ESI,dword ptr [0x00450bc4]   ; ESI = (obj+0x1c - len*9/2 + obj+0x10) - camera
    0041a9eb  TEST ESI,ESI / JGE
    0041a9f3  XOR ESI,ESI                      ; low clamp at 0
    0041aa0e  MOV ECX,0x31a / SUB ECX,EAX      ; 0x31a == 794
    0041aa15  CMP ESI,ECX / JLE
    0041aa2e  MOV ESI,0x31a / SUB ESI,EDX      ; high clamp at 794 - 9*len

There are FOUR MOV r32,0x31a sites in the function (0041aa0e, 0041aa2e, 0041abf0, 0041ac10) -- two clamp pairs, the ordinary name tag and the MENU_CLIP7 variant.

THE DISCRIMINATING RUN. A 794-based clamp and a view-based bound predict DIFFERENT freeze positions, so the two windows separate them: view-based would freeze at 1091 and 969 respectively, 794-based freezes at 785 in both.

    window 1100x550    x=785 on 1268 frames
    window 1920x1080   x=785 on 1268 frames

Identical, to the frame count. The tag tracks the fighter (609..782) and then stops dead at 785 = 794 - 9 for a one-character tag, while the fighter walks on into the width issue #43's widened bound opened.

WHY THIS IS RECORDED SEPARATELY FROM THE EARLIER #55 NOTES: three of them were written off comparisons whose two hypotheses predicted the SAME number (the camera was 0, or the bound was the same at both widths tested), and each read as an answer. This one was chosen so the two answers differ, and run against both.

## What would falsify it

a run at a view width W whose tag freezes at W-9 instead of 785 -- that would mean the bound follows the view and the clamp is not what pins it. Also falsified if a hand-port of fn_0041a5a0 whose two 0x31a sites read bg_view_width() leaves the tag frozen at 785.
