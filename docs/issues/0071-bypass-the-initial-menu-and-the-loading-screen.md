---
id: 71
title: Bypass the initial menu and the loading screen
status: resolved
symptom: Every run boots to the front-end menu on frame 1, then the mode menu, then a long data load before gameplay. There is no way to jump straight into a match
tags: reported,feature,startup
created: 2026-08-16
updated: 2026-08-20
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
