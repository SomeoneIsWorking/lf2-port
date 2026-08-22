---
id: C049
kind: claim
status: holds
created: 2026-08-22
tags: cursor,re
depends: runtime/overrides/guest_cursor.h#guest_cursor_draw, runtime/overrides/text.c#fn_0043f010
---

## Claim

LF2’s guest cursor has exactly three producers into fn_0043f010: return addresses 00424660, 00428778, and 004329ea, using the sheet in data slot 00451170.

## Evidence

LF2_CURSOR_TRACE at front end, mode menu, and game named the three callers; LF2_SMALL_BLT supplied the 11x19 positive control. After producer-exact removal, LF2_CURSOR_ON=1 cannot restore an 11x19 draw and the moved-pointer frame has no guest arrow.

## What would falsify it

if an 11x19 guest cursor draw appears from another producer or any listed caller proves to be non-cursor artwork
