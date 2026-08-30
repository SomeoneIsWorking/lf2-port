---
id: 115
title: Ship a release AppImage with GUI game-data discovery
status: resolved
symptom: A release AppImage launched without a terminal cannot explain where the user must place the required LF2 game files; it must find user-supplied data beside the AppImage and show an SDL dialog with the exact location when absent.
tags: reported,release,appimage,startup,ux
created: 2026-08-30
updated: 2026-08-30
---

## Root cause

The runtime assumes `bootstrap.py` supplied both the game-tree working directory and
`lf2.exe` argument. A desktop launch reaches `guest_load_image()` before SDL is initialized;
when the executable is absent it prints to a terminal that does not exist and aborts. Under
AppImage, `SDL_GetBasePath()` points inside the mounted payload, so external game data beside
the clicked AppImage must instead be resolved from the parent of `$APPIMAGE`.

The required input is the complete extracted LF2 v2.0a tree, not only `lf2.exe`: game startup
opens `data/data.txt` and the remaining assets by paths relative to the working directory.

The release may contain the compiled port, including its translated native code, but it may not
contain `lf2.exe`, the installer, or extracted game assets. The packaged executable therefore still
needs a player-owned complete LF2 v2.0a tree at runtime.

## What was tried / dead ends

- **Treat the translated native port as an unlicensed game-file payload:** superseded by the
  repository-wide release rule added on 2026-08-30. AppImage/APK packages contain the port and
  redistributable runtime resources, while excluding original executables and extracted assets.
- **Only place `lf2.exe` beside the AppImage:** incomplete; the game also needs its full
  extracted relative asset tree.
- **Use `SDL_GetBasePath()` for sibling data:** wrong under AppImage because it names the
  internal mount, not the directory containing the outer AppImage.

The release path must provide a native setup screen with Browse, validate a selected extracted
`lf2.exe` and its required sibling data, persist the selected game tree in the OS user-data/config
location, and support reselection. A `game/` tree beside the outer AppImage is also a valid
zero-configuration candidate. The original files remain outside the AppImage in every case.

## Resolution

### Resolution (2026-08-30)
Implemented CMake-owned AppDir installation, pinned and content-gated x86_64 AppImage packaging,
manual local release build/upload tooling, exact LF2 v2.0a executable plus sibling-data validation,
outer-APPIMAGE `game/` discovery, an SDL Browse/Quit first-run picker, persisted selection, and
`--select-game` reselection. Verified the extracted final artifact contains no original PE/game-data
paths; sibling-data AppImage boot reached frame 1 and exited 0; the missing-data path exited 2 and
named the exact sibling game directory. ZIP imports are now validated in a separate preparation
tree before the one accepted import is atomically replaced; the discriminator mutates the same ZIP
into an incomplete install and proves the previously accepted executable remains intact. No hosted
release workflow or generated package/source output is committed.
