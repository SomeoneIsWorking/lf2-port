---
id: 121
title: Accept the original LF2 installer in packaged setup
status: resolved
symptom: First-run Browse currently accepts an extracted lf2.exe, complete directory, or ZIP, but selecting LF2_v2.0a.exe as downloaded still requires manual extraction
tags: reported,release,setup,installer,android,appimage
created: 2026-08-30
updated: 2026-08-30
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
