---
id: 7
title: Game draws its own mouse cursor over the host's
status: resolved
symptom: a white arrow that is not the user's cursor, drawn by the game
tags: cursor,rendering,menu,method
created: 2026-08-05
updated: 2026-08-05
---

The game draws an 11x19 sprite at (pointer.x, pointer.y+2) from its own sheet, on top of the host cursor.

METHOD NOTE, the useful part: LF2_CURSOR_FIND (which scores blits by how constant their offset from the pointer is) could NOT find it, and confidently reported that no blit tracks the pointer. The blit is issued from inside fn_0043f010, which draws everything, so an 11x19 sprite was averaged in with full-screen backgrounds. Aggregating by call site is the wrong key when one call site draws the whole game. LF2_SMALL_BLT (list small destinations, distinct only) found it in one run.

Also a wrong turn worth not repeating: declining the whole sheet blanks the menu's character artwork (51492 pixels), because the sheet is shared. The cursor is the draw of that sheet landing ON the pointer.

The sheet handle is a heap pointer; the stable identity is the .data slot holding it, 0x00451170, found by scanning .data for the handle at the moment of the draw.

Fix: fn_0043f010 override declines the draw when sheet == LD32(0x00451170) and (x,y) == (mouse_x, mouse_y+2). LF2_CURSOR_ON=1 restores it.

Verified with a control: 11x19 blit count 4 -> 0, and 4 again with LF2_CURSOR_ON=1. NOTE: comparing frame dumps ACROSS runs is not a usable A/B here -- load timing differs, so frame N holds different content.
