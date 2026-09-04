---
id: C037
kind: claim
status: falsified
created: 2026-08-20
tags: renderer,background
depends: runtime/video/render.c#render_present, the recorded render runtime scenario, the recorded background runtime scenario
falsified_on: 2026-08-22
---

## Claim

The native engine no longer gives scrolling layers independent fractional sampling phases: it reproduces the integer-grid software composition, then scales the completed scene once.

## Evidence

The recorded renderer comparison measured a maximum channel difference of 2 on the match frame while both deliberate skip arms differed over 134,942 pixels. `render.c` submits the scene at scale 1 into the native composition target and performs one final nearest copy.

## What would falsify it

a scrolling capture shows one layer edge changing phase relative to another, or the engine no longer matches the integer-grid software compositor

## FALSIFIED 2026-08-22

FALSIFIED 2026-08-22 as a correctness goal: whole-scene scaling contradicts issue #41's full-resolution renderer contract and visibly left a band at 3840x1975. Its cited render/background evidence ran at 794x550, where scale is 1, so it could not distinguish a full-output per-draw target from a composition target enlarged afterward. Issue #76 is reopened for a per-draw sampling fix if fractional scrolling still jitters.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
