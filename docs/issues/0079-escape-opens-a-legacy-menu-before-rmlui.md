---
id: 79
title: Escape opens a legacy menu before RmlUi
status: closed
symptom: Pressing Escape opens the hand-rolled pause menu, and RmlUi can only be opened as a second-level Settings item; Escape must enter the RmlUi-owned menu flow directly
tags: reported,ui,rmlui,pause,ux
created: 2026-08-20
updated: 2026-09-09
---

## Root cause
`runtime/app/pause.c` was still a complete hand-painted menu and treated RmlUi as one nested
Settings item. Its Escape/Start ownership was match-only, so there was no global modal contract
and no single place that could withhold input from every guest path.

## What was tried / dead ends
Making the old menu open RmlUi preserved two navigation systems and could never satisfy global
access. SDL event consumption alone was also insufficient because the guest input override polls
host keyboard and controller state directly.

### Note (2026-08-20)
USER 2026-08-20: RmlUi must be accessible from every screen. Escape and the controller menu action must open the RmlUi-owned shell globally, with the underlying game paused only where gameplay actually needs freezing; there must be no legacy-menu prerequisite.

### Reopened (2026-08-21)
USER 2026-08-21 reports ESC does not open the menu in the running game. The ui_global route's LF2_KEY_SCRIPT is not evidence for physical SDL keyboard input; reproduce through SDL key events and trace the host key ledger before changing the toggle.
