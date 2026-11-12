---
id: 133
title: The text tile pool fills during an online session and text goes undrawn
status: open
symptom: LF2_RENDER_DEBUG on the host of online_meeting_test.py reports "837 tiles were NOT DRAWN -- the 512-texture pool was full" and 512 tile allocations; a local two-player match allocates 6.
state_items: S014
tags: netplay,rendering,text
created: 2026-11-12
updated: 2026-11-12
---

Reproduce: `LF2_RENDER_DEBUG=1 uv run --frozen python tools/routes/online_meeting_test.py`, then
read `scratch/online-meeting/host.log`. The busiest frame drew 512 tiles. Not yet traced: whether
tiles recorded while resimulated frames are not presented pile up into the next presented frame's
list, or the port menu's pages churn tile sizes.
