---
id: 105
title: Pause gameplay while the global RmlUi document is open
status: resolved
symptom: The global RmlUi menu consumes input but the match continues updating behind it; opening the menu must freeze gameplay while the document continues to render.
tags: reported,rmlui,pause
created: 2026-08-24
updated: 2026-08-25
---

## Root cause

RmlUi owned input while open but left LF2's three-stage match pause pipeline untouched, so the
ordinary world update continued behind the modal.

## What was tried / dead ends

Freezing presentation or retaining a screenshot would also freeze the UI/compositor and would not
stop guest state from advancing. The pause belongs at the game's update gate.

## Resolution

Opening RmlUi during a match snapshots LF2's effective/next/request pause words and pins the
effective state to paused while leaving its draw/present tail active. Closing restores the exact
snapshot. `ui_global` verifies outside-document pixels remain identical across the open interval
and then change after close, proving both freeze and resume.

### Resolution (2026-08-25)
RmlUi now snapshots and pins LF2 three-stage pause state during a match and restores it on close. ui_global proves frozen world pixels, active modal rendering, and immediate resume.
