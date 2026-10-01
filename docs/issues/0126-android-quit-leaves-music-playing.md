---
id: 126
title: Android quit leaves music playing
status: closed
symptom: Choosing QUIT GAME on Android leaves LF2 music playing after the game is expected to close.
tags: reported,android,audio,shutdown,ui
created: 2026-08-31
updated: 2026-09-09
---

### Reopened (2026-08-31)
User clarified that Android must not programmatically terminate the task: Home is the intended exit path. Remove the native-to-Java finishAndRemoveTask bridge; keep orderly SDL audio cleanup only for real process/lifecycle teardown.
