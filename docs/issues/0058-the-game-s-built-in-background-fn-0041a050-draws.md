---
id: 58
title: The game's built-in background (fn_0041a050) draws its fills 794 wide, so it stops short in a wide view
status: open
symptom: found while tracing issue #55, not reported and not yet seen on screen. fn_0041a050 draws a fixed backdrop with five full-width fills whose width is the literal 0x31a (794), so on any view wider than the game's own screen those bands stop at 794 and leave the rest unpainted
tags: widescreen,rendering,background
created: 2026-08-11
updated: 2026-08-11
---

FOUND 2026-08-11 while eliminating camera readers for issue #55. NOT a report, and NOT yet
observed in a frame -- filed because the constant is unambiguous and the next person to see a
half-painted backdrop should not have to find it again.

DECOMPILED, fn_0041a050 (502 bytes, tools/re/ghidra_scripts/DecompDump.py). It draws a fixed
backdrop rather than a stage's layers: a repeating element scrolled at camera/100, another at
camera*7/10, a fence pattern stepped every 0x46 from `(900 - camera) % 0x46`, and five solid
bands drawn through FUN_00415160(x, y, w, h, colour):

    FUN_00415160(0, 0x146, 0x31a, 0x14, 0x3f3f3f)
    FUN_00415160(0, 0x159, 0x31a, 0x9c, 0x575757)
    FUN_00415160(0, 0x1d7, 0x31a, 0x1e, 0x3f3f3f)
    FUN_00415160(0, 0x148, 0x31a, 2,   0x373737)
    ... and four more at 0x31a wide for the horizon lines

0x31a is 794. Every one of those bands is the game saying "the whole width of the screen", in
the only units it had. On a 978-wide composition they cover 81% of it.

WHAT IS NOT ESTABLISHED: which background this is and whether any route reaches it. Issue #3
records a built-in background 99 built from PE resources rather than from bg.dat, and this
function's hardcoded geometry fits that description, but the two have not been tied together.
Nothing in tools/routes/ selects it. So this could be a screen no player sees without asking
for it -- find that out before spending anything on the fix.

THE FIX, WHEN IT IS WANTED, is the substitution this port has made four times already: 794 is
the view, not a constant. The difference here is that these are the game's own literals inside
a recompiled function, so it means an override for fn_0041a050 rather than a value patched
somewhere -- 502 bytes, and the decompile above is the whole of it.

RELATED: issue #23 is the same SHAPE for a real stage's sky layer (a layer with less picture
than the view is wide), and #54 is the same shape again for the stage-mode UI row. Three
places where 794 was the width of the world.
