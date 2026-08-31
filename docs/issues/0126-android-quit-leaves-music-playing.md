---
id: 126
title: Android quit leaves music playing
status: resolved
symptom: Choosing QUIT GAME on Android leaves LF2 music playing after the game is expected to close.
tags: reported,android,audio,shutdown,ui
created: 2026-08-31
updated: 2026-08-31
---

REPORTED 2026-08-31. Trace the native quit path through host shutdown and the Android Activity boundary; close the music stream/device before task teardown. Android has no useful desktop-style Quit Game action, so remove or hide that choice only on Android while retaining desktop behavior.

### Resolution (2026-08-31)
Root cause: shutdown destroyed the SDL audio stream without first stopping the mixer or discarding buffered samples, allowing music to outlive Android task teardown. dsound_shutdown now clears guarded music state, pauses and clears the stream before destruction, then frees the PCM. Android binds quit_supported=false so the desktop Quit Game control is not rendered. Focused mixer/RmlUi/Android checks and full CTest passed.

### Reopened (2026-08-31)
User clarified that Android must not programmatically terminate the task: Home is the intended exit path. Remove the native-to-Java finishAndRemoveTask bridge; keep orderly SDL audio cleanup only for real process/lifecycle teardown.

### Resolution (2026-08-31)
Android did not need a programmatic task shutdown; Home is the intended exit path. Removed the native-to-Java finishAndRemoveTask bridge while retaining normal SDL resource cleanup only for an actual process exit. The Android Quit Game control remains hidden. On the shared API 35 emulator, Home then reopening LF2 retained the same PID (8175) and returned via a hot task launch; the Android source/package test and Clang build pass.
