---
id: C027
kind: claim
status: falsified
created: 2026-08-12
tags: re,screens
depends: runtime/overrides/world.h
falsified_on: 2026-08-21
---

## Claim

0x0044d020 is the game's screen selector: 0 = the match, 1/2/3 = character selection, 10 = the FRONT-END MENU (fn_00431d10), 0x14..0x32 / 0x78..0x96 / 200..299 / 300 = the other panels. 0x00451160 is the front-end cursor AND the game mode, one word.

## Evidence

Read from Ghidra decompilations of fn_0041bc90 (hands the word by address to fn_00429730), fn_00429730 (the dispatch chain) and fn_00431d10 (the confirm branch, which writes 3). Confirmed on a run: tools/e2e.sh exit_to_menu reports LANDED on screen 10 -- the FRONT-END MENU, and lands on 1 when the fix under test is compiled out.

## What would falsify it

a run where the front-end menu is drawn while 0x0044d020 reads something other than 10, or a fifth writer of the word outside fn_0041bc90 and fn_00429730's callees

## FALSIFIED 2026-08-21

The address mapping was right but the screen name was wrong. The discarded first screen is top-level mode 0 in fn_004246b0; screen word 10 is fn_00431d10's post-load eight-item VS/Stage/Championship mode menu. The direct constructor boot and exit_to_menu route both reach screen 10 without ever entering the launcher.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
