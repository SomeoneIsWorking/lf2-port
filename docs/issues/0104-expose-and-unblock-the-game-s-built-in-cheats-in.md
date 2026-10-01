---
id: 104
title: Expose and unblock the game's built-in cheats in RmlUi
status: closed
symptom: The executable's built-in cheats (reported around F2/F3) are blocked by default and cannot be discovered or activated through the port UI.
tags: reported,cheats,rmlui,re
created: 2026-08-24
updated: 2026-09-09
---

## Root cause
The cheats were guest function-key actions, not a general function-key feature: the hidden roster
code is `LF2.NET`, while F6 through F9 toggle unlimited MP, restore fighters, drop items, and destroy
items. The binary gated F6-F9 on VS/hidden-code state and let F3 permanently set the "Function Keys
Locked" state. The port UI exposed none of them.

## What was tried / dead ends
Listing F1-F12 or exposing the lock state would reproduce implementation details rather than the
five actual player actions. The RmlUi page therefore derives only from the typed cheat descriptor
table.
