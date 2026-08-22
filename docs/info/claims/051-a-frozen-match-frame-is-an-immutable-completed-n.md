---
id: C051
kind: claim
status: falsified
created: 2026-08-22
tags: renderer,rmlui,pause
depends: runtime/video/render.c#render_present, runtime/video/render_snapshot.c#render_snapshot_capture, runtime/video/ddraw.c#present_primary, tools/routes/ui_global_test.py
falsified_on: 2026-08-22
---

## Claim

A frozen match frame is an immutable completed native output captured before RmlUi, not retained display-list metadata; opening and mapped-closing the modal preserve the completed frame's pixels.

## Evidence

Pre-fix deterministic 1920x1080 ui_global captures: frame 1574 was the first hidden frame and had 0.824556 non-black coverage versus 0.885122 before open (93.2%), with black gaps and garbled tiles. After the snapshot change, tools/e2e.py ui_global captured six transition frames, proved modal appearance/removal, and measured 0.8851 versus 0.8851; pause_dropout measured 121 native snapshot presents. Full ctest 26/26 passed.

## What would falsify it

If an exact ui_global open/close capture shows the first hidden frame below 98% of the pre-open non-black coverage, or if a frozen native present can use a snapshot from another composition source.

## FALSIFIED 2026-08-22

The user observed a remaining transient close glitch and corrected the intended contract: RmlUi must use the game's normal render path, not an immutable completed-frame snapshot. The snapshot design therefore does not satisfy the visible transition or product requirement.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
