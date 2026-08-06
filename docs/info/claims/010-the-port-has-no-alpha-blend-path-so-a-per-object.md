---
id: C010
kind: claim
status: holds
created: 2026-08-05
tags: rendering,coop
depends: runtime/ddraw.c
reconfirmed: 2026-08-06
verified_at: 2026-08-06 13:29:33
---

## Claim

The port has NO alpha/blend path, so a per-object fade cannot be expressed with the game's own drawing -- a flashing object is the existence gate toggled on a schedule

## Evidence

runtime/ddraw.c's blit is a colour-keyed copy over 8-bit paletted sprites (DDBLT_KEYSRC plus the surface's key range); grep for alpha/blend across runtime/ddraw.c finds only the struct-offset note about the ten z-buffer and alpha members the game never uses. The flash built on the gate byte was measured instead: gate transitions logged with their frames on an 8-frame period (2308 hidden, 2316 shown, 2324 hidden, 2332 shown), 17 cycles before lock-in.

## What would falsify it

a blend path appearing in the blit (a real alpha argument reaching Blt, or a paletted fade LUT), which would make a genuine fade expressible without inventing per-object blending in the porting layer

## Re-confirmed 2026-08-06

Re-verified 2026-08-06 by reading the two places a blend could live, after six commits to runtime/ddraw.c had made the claim stale. (1) The software blitter, blit() at ddraw.c:620, has exactly two per-pixel behaviours in both its fast and indexed paths: 'drow[i] = srow[i] & 0x00ffffff' (straight copy) and, when keyed, 'if (v >= lo && v <= hi) continue' (skip). No multiply, no add, no per-pixel weight of any kind. (2) The present path does not blend either -- grep for BlendMode/SDL_BLEND/alpha across ddraw.c and win32.c returns nothing outside comments, and hostwin_present is a memcpy into one streaming XRGB8888 texture drawn as a single opaque quad. So a per-object fade still cannot be expressed, and issue #30's cast shadows and bloom cannot be built until a blend stage exists.

## Re-confirmed 2026-08-06

Baseline re-stamped with a second-precision timestamp; the evidence is the re-verification recorded immediately above (blit() has only copy and colour-key-skip; no BlendMode/alpha anywhere in ddraw.c or win32.c).
