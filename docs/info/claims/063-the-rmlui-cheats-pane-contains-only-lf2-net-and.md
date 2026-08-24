---
id: C063
kind: claim
status: holds
created: 2026-08-25
tags: cheats
depends: runtime/overrides/cheats.c, runtime/app/function_keys.c, runtime/ui/settings_ui.cpp, tools/routes/cheats_test.py
---

## Claim

The RmlUi Cheats pane contains only LF2.NET and F6-F9, and an RmlUi F6 activation reaches LF2 exactly once without the original lock gates.

## Evidence

2026-08-25 ctest cheats/function_keys passed; tools/e2e.py cheats opened the pane, fired every action, released F6, incremented LF2 counter 00450c18 once, and toggled 0044d034.

## What would falsify it

The pane exposes a non-cheat function key or lock option, an action remains blocked by F3/mode/hidden code, or one activation is missed/repeated/left held.
