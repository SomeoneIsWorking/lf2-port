---
id: 121
title: Accept the original LF2 installer in packaged setup
status: resolved
symptom: First-run Browse currently accepts an extracted lf2.exe, complete directory, or ZIP, but selecting LF2_v2.0a.exe as downloaded still requires manual extraction
tags: reported,release,setup,installer,android,appimage
created: 2026-08-30
updated: 2026-08-31
---

## Root cause

`game_selection_resolve` treated only `.zip` as an import container. Every `.exe` was sent to
`game_data_validate_executable`, so the original `LF2_v2.0a.exe` installer was correctly rejected as
not being the title executable but no shipping code could decode its custom `wwgT` overlay. The
stdlib Python extractor used by source bootstrap could not serve AppImage or APK runtime setup.

Android also had a separate ownership-boundary defect: native ZIP extraction wrote to the app-data
root, outside the Activity's unique `game-import-*` staging wrapper, so Java's atomic commit refused
the otherwise validated result.

## What was tried / dead ends

The packaged runtime does not launch `tools/extract_game.py`: Python is not an APK runtime and an
AppImage first-run flow cannot depend on a terminal or host interpreter. The title-specific container
decoder therefore belongs in the native app boundary; the generic ZIP path remains in Lucent.

## Resolution

### Resolution (2026-08-30)
Added bounded in-process zlib/bzip2 decoding of the original LF2 v2.0a installer, routed desktop and Android first-run selection through it, kept Android archive preparation inside Activity-owned staging, and verified the native output byte-for-byte against all 690 files from the existing Python extractor. Desktop Clang build/48-test gate passed; AppImage content gate passed; Android NDK/Gradle 9.7.1 debug APK build and content inspection passed. Hardware Android acceptance remains tracked by issue #120.

### Reopened (2026-08-30)
REPORTED 2026-08-30: On Android, pressing Choose Folder and selecting the installer executable made the app quit. The published v0.1.3 APK therefore falsifies the claimed working installer first-run path; reproduce the Activity result/selection-state transition and fix the actual handoff, not the exit symptom.

### Note (2026-08-30)
Investigation: no ADB device is connected and the configured ARM64 AVD points to a missing system image, so the published APK cannot be replayed locally yet. Static tracing separates two paths: LF2_v2.0a.exe is copied as installer.exe and handed directly to native extraction; exact lf2.exe opens ACTION_OPEN_DOCUMENT_TREE because its sibling assets are required. A null/cancelled tree result intentionally returns SETUP_UI_CANCELLED and main exits, while a returned invalid folder should loop to setup rather than quit. Need the exact selected filename and Android logcat/process crash to distinguish cancellation/activity destruction from a native post-import crash.

### Note (2026-08-31)
Reproduced on the persistent API 35 Pixel 7 AVD (codex_shared_api35): SAF selected LF2_v2.0a.exe from Downloads, Java/native extraction returned to the landscape Activity, but setup refused with 'that installer produced an invalid executable path'. App-private files contained no imported game tree, so this is an Android installer-result/commit contract failure rather than file-picker cancellation.

### Resolution (2026-08-31)
Android SAF installer imports failed because game_data_validate_executable canonicalized lf2.exe while validation compared it with the noncanonical staging root; /data/data and /data/user/0 aliases made a valid path appear outside staging. Canonicalize the preparation root before deriving the relative executable, with a symlink regression test. The signed release APK then imported the original installer on codex_shared_api35 and reached the landscape game menu.
