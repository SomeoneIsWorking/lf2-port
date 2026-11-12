---
id: 131
title: A frame at the VS match load runs differently from its snapshot
status: open
symptom: LF2_ROLLBACK_CHECK on a one-player VS route logs "frame 1123 differs on rerun", the frame before the match HUD first shows; every other frame of 2100 reruns the same.
state_items: S023
tags: netplay,rollback,determinism
created: 2026-11-12
updated: 2026-11-12
---

Reproduce: `LF2_ROLLBACK_CHECK=1 LF2_UNPACED=1 LF2_WINDOW=hidden` with the VS pad route of
`tools/routes/leave_match_test.py` and `LF2_QUIT_AFTER=2200`; `match` first up at frame 1126.
The match load creates five COM surfaces (#516-520), so the snapshot before it holds fewer
objects than the frame that ran. Not yet traced: which state the rerun leaves different, and
whether `com_restore`'s placeholders or the VRAM cursor are its cause. Online play through the
load kept equal final hashes on both peers, so the difference does not reach the logic hash there.
