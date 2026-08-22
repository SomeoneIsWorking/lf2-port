---
id: C050
kind: claim
status: holds
created: 2026-08-22
tags: ui,re
depends: runtime/overrides/menu.c#fn_004246b0, runtime/overrides/boot_guest.h#boot_guest_target_mode
---

## Claim

The pictured bilingual controls table is launcher sub-screen 6 in fn_004246b0, entered from launcher item 3; it is not the port’s controls hint or RmlUi controls page.

## Evidence

Ghidra decompilation scratch/decomp/004246b0.c shows the item-3 write DAT_0044d064=6 and the complete screen-6 table/editor branch. The project top-level mode is a separate first dword of the world object.

## What would falsify it

if a call path reaches the pictured table while the top-level mode is loader/game proper, or the decompilation identity is disproved
