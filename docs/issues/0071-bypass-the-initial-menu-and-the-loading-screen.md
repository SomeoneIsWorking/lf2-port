---
id: 71
title: Bypass the initial menu and the loading screen
status: closed
symptom: Every run boots to the front-end menu on frame 1, then the mode menu, then a long data load before gameplay. There is no way to jump straight into a match
tags: reported,feature,startup
created: 2026-08-16
updated: 2026-09-09
---

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

### Reopened (2026-08-20)
USER 2026-08-20 reports startup still skips screens by injecting a button. The resolution claim is therefore untrusted: trace the default boot path and replace any synthetic confirm/button state with the game's actual post-platform loader/menu state transition.

### Note (2026-08-20)
USER 2026-08-20 additionally reports the loading screen itself is still visible. Required observable remains: the real loader runs, but no front-end or loading frame is presented; the first visible interactive frame is the mode menu.

### Note (2026-08-24)
2026-08-24 correction (tracked as reported issue #102): the 2026-08-21 resolution was not a bypass. It kept top mode 1, hid SDL, discarded host presents, and called loading complete when top mode became 2 even though fn_0041bc90's one-time data initializer had not run yet. The corrected route creates SDL visible, bypasses mode 1, and calls that initializer synchronously with progress drawing/presentation scoped off at the guest boundary.
