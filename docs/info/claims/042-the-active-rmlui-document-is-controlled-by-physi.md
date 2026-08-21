---
id: C042
kind: claim
status: holds
created: 2026-08-21
tags: rmlui,input
depends: runtime/ui/rmlui_input.cpp#rmlui_input_update, runtime/ui/rmlui_input.cpp#rmlui_input_pointer_event
reconfirmed: 2026-08-21
verified_at: 2026-08-21 10:55:20
---

## Claim

The active RmlUi document is controlled by physical mouse, configured keyboard actions, and mapped controller actions through one UI-input boundary.

## Evidence

tools/e2e.py ui_escape passed physical XTEST Escape, a window-relative Graphics click, and mapped keyboard Attack; tools/e2e.py settings passed four controller moves plus mapped Confirm into Controls on 2026-08-21.

## What would falsify it

any physical pointer, configured keyboard action, or attached mapped controller can no longer navigate or activate the visible document

## Re-confirmed 2026-08-21

Verified at commit e005304 by tools/e2e.py ui_escape and settings: physical mouse selection, configured keyboard Attack activation, post-close game input, and mapped navigation/confirmation from controller slot four all passed.
