---
id: C037
kind: claim
status: holds
created: 2026-08-20
tags: renderer,background
depends: runtime/video/render.c#render_present, tools/routes/render_test.sh, tools/routes/background_test.sh
---

## Claim

The native engine no longer gives scrolling layers independent fractional sampling phases: it reproduces the integer-grid software composition, then scales the completed scene once.

## Evidence

tools/e2e.sh render: engine vs software max channel diff 2 on the match frame, while both deliberate skip arms differ over 134942 pixels. tools/e2e.sh background: two native frames are byte-identical to the recompiled background body at native width and both negative arms differ. render.c submits the scene at scale 1 into the native composition target and performs one final nearest copy.

## What would falsify it

a scrolling capture shows one layer edge changing phase relative to another, or the engine no longer matches the integer-grid software compositor
