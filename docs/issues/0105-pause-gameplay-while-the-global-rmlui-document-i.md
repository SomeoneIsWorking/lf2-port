---
id: 105
title: Pause gameplay while the global RmlUi document is open
status: closed
symptom: The global RmlUi menu consumes input but the match continues updating behind it; opening the menu must freeze gameplay while the document continues to render.
tags: reported,rmlui,pause
created: 2026-08-24
updated: 2026-09-09
---

## Root cause
RmlUi owned input while open but left LF2's three-stage match pause pipeline untouched, so the
ordinary world update continued behind the modal.

## What was tried / dead ends
Freezing presentation or retaining a screenshot would also freeze the UI/compositor and would not
stop guest state from advancing. The pause belongs at the game's update gate.
