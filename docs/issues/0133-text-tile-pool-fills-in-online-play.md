---
id: 133
title: The text tile pool fills during an online session and text goes undrawn
status: resolved
symptom: LF2_RENDER_DEBUG on the host of online_meeting_test.py reports "837 tiles were NOT DRAWN -- the 512-texture pool was full" and 512 tile allocations; a local two-player match allocates 6.
state_items: S014
tags: netplay,rendering,text
created: 2026-11-12
updated: 2026-11-12
---

Reproduce: `LF2_RENDER_DEBUG=1 uv run --frozen python tools/routes/online_meeting_test.py`, then
read `scratch/online-meeting/host.log`. The busiest frame drew 512 tiles.

Cause: `runtime/video/host_frame.c:hostwin_present` returned before `render_frame_reset()` for a
frame that is not shown, so every rerun frame's draws stayed in the display list and piled into the
next shown frame, up to eight frames' text against the 512-texture pool.

Fix: an unshown frame is reset too. The same run now serves 2831 tile draws from 6 pooled
textures, with no tile undrawn and the match text on screen.
