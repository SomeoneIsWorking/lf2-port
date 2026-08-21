---
id: C042
kind: claim
status: holds
created: 2026-08-21
tags: rmlui,input
depends: runtime/ui/rmlui_input.cpp#rmlui_input_update, runtime/ui/rmlui_input.cpp#rmlui_input_pointer_event
---

## Claim

The active RmlUi document is controlled by physical mouse, configured keyboard actions, and mapped controller actions through one UI-input boundary.

## Evidence

tools/e2e.py ui_escape passed physical XTEST Escape, a window-relative Graphics click, and mapped keyboard Attack; tools/e2e.py settings passed four controller moves plus mapped Confirm into Controls on 2026-08-21.

## What would falsify it

any physical pointer, configured keyboard action, or attached mapped controller can no longer navigate or activate the visible document
