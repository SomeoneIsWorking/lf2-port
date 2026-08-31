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
