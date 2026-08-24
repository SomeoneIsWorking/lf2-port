---
id: 102
title: Loading bypass hides window instead of bypassing loading screen
status: resolved
symptom: The game window is hidden until loading completes; loading still advances through the loading-screen path instead of performing the load in a quick synchronous function call.
tags: reported,loading,window,override
created: 2026-08-24
updated: 2026-08-24
---

## Root cause

Startup treated top-level mode 2 as "data loaded", but mode 1 only cleaned/drew the loading
picture, selected mode 2, and presented it. The actual synchronous one-time data initializer is
the `0x0041be98..0x0041c57b` block in `fn_0041bc90`, gated by `0x0044d05c`. Hiding SDL and gating
host presents therefore concealed unchanged loading-screen control flow.

The first direct-call implementation exposed a second invariant: the old startup hook clobbered
guest ECX before its shadow `fn_004246b0` call, accidentally entering mode 0 and running that
branch's `DAT_0044d068` keyboard-table/menu-resource initializer. Calling only the data initializer
left those tables zero, so character selection could not complete. This initializer has no smaller
guest function and must remain owned by the shadow body.

## What was tried / dead ends

Calling the mode-1 cleanup/update helpers around `fn_0041bc90` did not restore character-select
input because neither owns the zeroed tables. An isolated committed-tree control plus semantic
`.data` snapshots identified the missing mode-0 initializer.

### Resolution (2026-08-24)
Removed the hidden-window/present-gate path. Startup now creates SDL visible, synchronously runs the guest-owned mode-0 resource initializer with drawing/presentation declined, skips mode 1, and directly calls fn_0041bc90's one-shot data initializer. Physical X11 observation saw the window before load completion; paced smoke reached match with 17/17 keys, 19% CPU, clean exit.
