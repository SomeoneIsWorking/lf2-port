---
id: 9
title: Navy rectangles on the stage ground: DDBLTFX.dwFillColor read from the wrong offset
status: resolved
symptom: stage 1-1 draws navy-blue rectangles over the ground; the ground band is 45% navy instead of green
tags: rendering,ddraw,colorfill,stage,background
created: 2026-08-05
updated: 2026-08-05
---

`DDBLT_COLORFILL` took `dwFillColor` from **offset 16** of `DDBLTFX`. That member is
`dwRotationAngle`. The real `dwFillColor` is the last union in the struct, at **offset 80**
of 100 bytes. The callers never write offset 16, so every colour fill in the game painted a
leftover stack dword.

Chain, each step measured:

1. `LF2_FRAME_DUMP` on stage 1-1 reproduced it. The navy is exactly `(0,0,56)` = `0x000038`,
   and no palette in `bg/sys/lf/*.bmp` contains that colour, so it is not sprite data.
2. Connected components: the ground band `(0,356)-(793,527)` is one navy blob at 45% fill,
   i.e. the navy is *underneath* and the layer art only covers 55% of it.
3. `LF2_BLT_FRAME=3900` (new, see below) listed all 142 blits of that frame. The ground
   layers `land1/land2/land4` are drawn at four scattered positions and `land3` not at all
   -- that is the game's own layout, not a defect. Nothing else covers the band.
4. Moving the hook ABOVE the colour-fill branch exposed the missing draw: a
   `COLORFILL` over exactly `(0,356)-(794,528)` -- the whole ground band -- with
   `dwFillColor = 0x000038`.
5. `FUN_00415160`, the fill helper, decompiles to a 0x64-byte frame whose only two stores
   are `dwSize = 100` at +0 and the colour at **+0x50**. That is the ground truth for the
   offset; the DirectDraw header was not consulted.
6. With the offset corrected the same fill reads `0x00104f10` = `(16,79,16)`, which is
   `land1.bmp`'s palette entry 0 -- the ground green the layer art blends into. The
   full-screen clear goes from a garbage `0x00023400` to `0x00000000`.

Verified on a stage 1-1 frame dump: no navy anywhere, ground solid green. All 9 ctest
targets pass, including the three slow end-to-end ones.

**Instrument note.** The first pass at this concluded "the ground is drawn by four layer
blits and nothing else", which was wrong, because the blit log sat *after* the colour-fill
early return and could not see a fill at all. `LF2_BLT_FRAME=<frame>[,...]` replaces the
old `LF2_BLT_ALL` (capped at the first 24 blits of the whole run -- all menu, no
denominator) and is called before that branch, printing `COLORFILL=<argb>` for fills and a
`N blits total` line so a short list is distinguishable from a truncated one.
