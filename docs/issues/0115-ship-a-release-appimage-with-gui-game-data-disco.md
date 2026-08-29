---
id: 115
title: Ship a release AppImage with GUI game-data discovery
status: investigating
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

There is a separate release constraint. The current `lf2` ELF includes 87,940 lines of
machine-translated LF2 code generated from the user's `game/lf2.exe`. Publishing that ELF in a
conventional prebuilt AppImage would violate this repository's rule against distributing
derivative or reconstructable game content.

## What was tried / dead ends

- **Conventional prebuilt AppImage:** rejected pending an explicit policy decision because it
  embeds the translated game program even if the original `lf2.exe` is still requested at
  runtime.
- **Only place `lf2.exe` beside the AppImage:** incomplete; the game also needs its full
  extracted relative asset tree.
- **Use `SDL_GetBasePath()` for sibling data:** wrong under AppImage because it names the
  internal mount, not the directory containing the outer AppImage.

A redistributable public artifact would need to extract and compile from the user's adjacent
installer/game tree on first launch. To remain a self-contained, terminal-free AppImage, that
design must also bundle its compiler/linker rather than silently depend on a developer
toolchain. A conventional AppImage can only be a private/local build unless the distribution
policy changes.

## Resolution
