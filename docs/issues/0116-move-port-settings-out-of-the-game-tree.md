---
id: 116
title: Move port settings out of the game tree
status: resolved
symptom: The AppImage setup path must persist the selected LF2 tree, but config.c defaults lf2.cfg to the current working directory. That is the user-supplied game tree for the port and a read-only/transient location for packaged launches; persistent settings must use the platform user-data/config directory while LF2_CONFIG remains a developer override.
tags: release,config,persistence,appimage
created: 2026-08-30
updated: 2026-08-30
---

## Root cause

`config.c` treated the process working directory as application-owned storage. That happened to
work only because developer launches changed into the extracted game tree before startup. A desktop
package can be mounted read-only and the player-owned game tree is an input, not a settings owner;
using either location couples persistence to deployment and can fail without a visible terminal.

## What was tried / dead ends

- **Keep `lf2.cfg` beside `lf2.exe`:** wrong ownership. The selected tree may be read-only, moved,
  shared between installations, or outside the AppImage's writable namespace.
- **Write beside the AppImage:** also wrong. The outer image location is not guaranteed writable and
  would make settings portable by accident rather than per-user by contract.
- **Use one process-relative filename and change cwd later:** still has two authorities; startup
  order would decide whether settings land in the checkout, game tree, or launcher directory.

## Resolution

### Resolution (2026-08-30)
Moved `lf2.cfg` to the OS-owned per-user path: Lucent owns platform resolution (XDG on Linux,
Application Support on macOS, AppData on Windows, and an Activity-supplied private root on Android),
while `runtime/app/user_paths.c` composes the LF2 filename. `LF2_CONFIG` remains the explicit
diagnostic override. Expanded config values to persist full paths including spaces and added
cross-process XDG write/read tests.
