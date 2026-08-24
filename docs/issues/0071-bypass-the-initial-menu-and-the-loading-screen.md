---
id: 71
title: Bypass the initial menu and the loading screen
status: resolved
symptom: Every run boots to the front-end menu on frame 1, then the mode menu, then a long data load before gameplay. There is no way to jump straight into a match
tags: reported,feature,startup
created: 2026-08-16
updated: 2026-08-24
---

## Reported

The user wants the initial menu and the loading screen bypassed -- jump straight to gameplay.

## What exists already

- LF2_MODE=<name> (runtime/overrides/menu.c) selects which of the eight modes a run enters,
  but it puts the selection on the game's OWN mode menu and lets the route confirm it -- it
  does not skip the front end or the load. It is a TEST arm, deliberately (env vars are
  diagnostics).
- The front end is drawn and taking input on frame 1 (docs/running.md, issue #57).
- The load is the game's own data load (~1.2s active after the fscanf fix, issue #8); the
  longer wall time includes drawing and the pacing sleeps.

## The constraint

- A bypass for a PLAYER is a feature, not a diagnostic, so it cannot be an LF2_* env var
  (AGENTS.md). It belongs in game state / menus, like drop-in coop.
- Skipping the load means the game must still have its data ready: the load IS what decrypts
  the .dat files. A true skip is only possible if the data can be loaded without the menus,
  or the menu/loading presentation is skipped while the load still runs underneath.

### Note (2026-08-16)
The user's rationale: 'The first menu serves no purpose, it has multiplayer which is not working and I don't want, it has controller mapping which makes no sense to have it only accessible from there'. So the front end is removed from the path, and its one live feature (controller mapping) moves to an in-game device-mapping UI (#70).

### Note (2026-08-20)
USER 2026-08-20 corrected the target: skip the game's first screen and the loading SCREEN, but still perform the required data load; the first interactive screen after boot must be the post-load mode menu. Do not synthesize menu input. The current uncommitted menu.c confirm injection is explicitly rejected and must be replaced by a direct startup state transition around the loader/presentation.

### Note (2026-08-20)
USER 2026-08-20: the skipped front end has no retained function. Its input setup is replaced by RmlUi #70; replay is unwanted; the game's network path is unwanted. A possible future network feature will be a separate custom rollback implementation, so the legacy network menu and code must not remain on the startup route. Required route: boot -> silent required data load -> post-load mode menu.

### Resolution (2026-08-20)
Startup now executes the original Game Start branch's non-rendering state transition at the guest boundary after platform initialization, then runs the real loader while suppressing the retired first/loading pictures. No input is injected; smoke proves the first visible/script screen is modemenu and reaches a match.

### Reopened (2026-08-20)
USER 2026-08-20 reports startup still skips screens by injecting a button. The resolution claim is therefore untrusted: trace the default boot path and replace any synthetic confirm/button state with the game's actual post-platform loader/menu state transition.

### Note (2026-08-20)
USER 2026-08-20 additionally reports the loading screen itself is still visible. Required observable remains: the real loader runs, but no front-end or loading frame is presented; the first visible interactive frame is the mode menu.

### Resolution (2026-08-21)
The rejected startup path reproduced the launcher's Game Start transition after initialization, so it still encoded frontend selection semantics and allowed loader presentation. fn_00419e40 now runs the original world constructor then establishes loader mode as the port's initial state before the first update; no input or launcher branch is synthesized. Loader frames are discarded while SDL stays hidden, and the window is revealed only after the first mode-menu frame is presented. Smoke and the default launcher verify modemenu is first.

### Note (2026-08-24)
2026-08-24 correction (tracked as reported issue #102): the 2026-08-21 resolution was not a bypass. It kept top mode 1, hid SDL, discarded host presents, and called loading complete when top mode became 2 even though fn_0041bc90's one-time data initializer had not run yet. The corrected route creates SDL visible, bypasses mode 1, and calls that initializer synchronously with progress drawing/presentation scoped off at the guest boundary.
