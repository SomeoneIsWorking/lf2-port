---
id: C027
kind: claim
status: holds
created: 2026-08-12
tags: re,screens
depends: runtime/overrides/world.h
---

## Claim

0x0044d020 is the game's screen selector: 0 = the match, 1/2/3 = character selection, 10 = the FRONT-END MENU (fn_00431d10), 0x14..0x32 / 0x78..0x96 / 200..299 / 300 = the other panels. 0x00451160 is the front-end cursor AND the game mode, one word.

## Evidence

Read from Ghidra decompilations of fn_0041bc90 (hands the word by address to fn_00429730), fn_00429730 (the dispatch chain) and fn_00431d10 (the confirm branch, which writes 3). Confirmed on a run: tools/e2e.sh exit_to_menu reports LANDED on screen 10 -- the FRONT-END MENU, and lands on 1 when the fix under test is compiled out.

## What would falsify it

a run where the front-end menu is drawn while 0x0044d020 reads something other than 10, or a fifth writer of the word outside fn_0041bc90 and fn_00429730's callees
