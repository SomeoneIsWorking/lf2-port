---
id: 79
title: Escape opens a legacy menu before RmlUi
status: resolved
symptom: Pressing Escape opens the hand-rolled pause menu, and RmlUi can only be opened as a second-level Settings item; Escape must enter the RmlUi-owned menu flow directly
tags: reported,ui,rmlui,pause,ux
created: 2026-08-20
updated: 2026-08-21
---

## Root cause

`runtime/app/pause.c` was still a complete hand-painted menu and treated RmlUi as one nested
Settings item. Its Escape/Start ownership was match-only, so there was no global modal contract
and no single place that could withhold input from every guest path.

## What was tried / dead ends

Making the old menu open RmlUi preserved two navigation systems and could never satisfy global
access. SDL event consumption alone was also insufficient because the guest input override polls
host keyboard and controller state directly.

## Resolution

`pause.c` now owns only lifecycle, match-freeze policy, and game actions. RmlUi is the sole visual
menu, Escape/Start reaches it from every supported screen, and both SDL events and the direct guest
gather are isolated while it is active.
### Note (2026-08-20)
USER 2026-08-20: RmlUi must be accessible from every screen. Escape and the controller menu action must open the RmlUi-owned shell globally, with the underlying game paused only where gameplay actually needs freezing; there must be no legacy-menu prerequisite.

### Resolution (2026-08-21)
The hand-painted pause menu owned Escape and exposed RmlUi only as a child Settings item. pause.c is now only lifecycle/action policy; Escape or Start opens the single RmlUi shell directly on modemenu, charselect, overlay, and match. RmlUi consumes all physical input, match-only freeze rewinds the retained native frame, and ui_global plus pause_dropout verify the four contexts and retained rendering.

### Reopened (2026-08-21)
USER 2026-08-21 reports ESC does not open the menu in the running game. The ui_global route's LF2_KEY_SCRIPT is not evidence for physical SDL keyboard input; reproduce through SDL key events and trace the host key ledger before changing the toggle.

### Resolution (2026-08-21)
The reopened failure was in the physical SDL path, not RmlUi. `hostwin_pump` continued Escape before `push_message`, the sole owner of the held-key ledger, so physical Escape never reached `pause_tick` while `LF2_KEY_SCRIPT` did. After moving physical key tracking ahead of modal consumption, a second defect remained: SDL can drain down+up in one pump, so held-state polling misses an ordinary tap. `runtime/input/keyboard.c` now owns every key transition and latches a fresh Escape down edge for one-shot app consumption; owned keys still never enter LF2. `tools/e2e.py ui_escape` uses a focused X11 window plus XTEST—not scripted input—and proves physical Escape opens then closes one rendered RmlUi document.
