---
id: 89
title: Remove the full-screen controls hint table
status: resolved
symptom: A large bilingual keyboard-controls table covers the game screens and should not be shown
tags: reported,ui,rendering,controls-hint
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

The table is not the port's one-line input hint or its RmlUi Controls page. It is sub-screen 6
inside the original 4,689-line launcher body `original guest routine 0x004246b0`, entered from launcher item 3.
Direct startup changed the constructor's initial top-level mode, but the update boundary did not
retire top-level mode 0 itself. If that launcher state reached the body, the entire old launcher,
including its control editor, became dispatchable again over the retained frame.

## What was tried / dead ends

Filtering the table's blits or text would only leave an invisible, interactive control editor.
Special-casing sub-screen 6 would also leave every other retired launcher branch reachable.

## Resolution

### Resolution (2026-08-22)
Top-level mode 0 is now routed through the real loader at the update boundary, before the
original body can dispatch it. Unknown modes are deliberately left visible rather than being
silently treated as loader state. The original control editor and all sibling launcher branches
are no longer dispatchable.
