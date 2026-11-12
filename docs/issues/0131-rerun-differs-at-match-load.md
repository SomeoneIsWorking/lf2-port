---
id: 131
title: A frame at the VS match load runs differently from its snapshot
status: resolved
symptom: LF2_ROLLBACK_CHECK on a one-player VS route logs "frame 1123 differs on rerun", the frame before the match HUD first shows; every other frame of 2100 reruns the same.
state_items: S023
tags: netplay,rollback,determinism
created: 2026-11-12
updated: 2026-11-12
---

Reproduce: `LF2_ROLLBACK_CHECK=1 LF2_UNPACED=1 LF2_WINDOW=hidden` with the VS pad route of
`tools/routes/leave_match_test.py` and `LF2_QUIT_AFTER=2200`; `match` first up at frame 1126.
Cause: `runtime/win32/gdi.c` keeps its bitmap and DC handle tables (counts, `dc_bitmap`, the
LoadImage latch) in host state no snapshot held. The match load makes twelve GDI objects, so the
rerun handed out handles twelve higher, and the game keeps them at guest 0x002fe6f8 onward; that
was the only differing page.

Fix: the tables are a snapshot section (`gdi_state_save`/`gdi_state_load`); loading truncates them
and frees the bitmaps loaded since. The check now reruns all 2100 frames of the route, the match
load included, with none differing, and names the pages of any frame that does.
